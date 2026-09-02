#include "application/ssh/KnownHostsController.h"

#include "infrastructure/ssh/KnownHostsStore.h"
#include "infrastructure/ssh/OpenSshKnownHostsImporter.h"

#include <QByteArray>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

namespace
{

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString fingerprint(const ztermy::ssh::KnownHostEntry &entry)
{
    const QByteArray key(reinterpret_cast<const char *>(entry.encodedKey.data()),
                         static_cast<qsizetype>(entry.encodedKey.size()));
    const QByteArray digest = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    ztermy::ssh::ObservedHostKey observed{
        .algorithm = entry.algorithm,
        .encodedKey = entry.encodedKey,
    };
    std::copy_n(reinterpret_cast<const std::uint8_t *>(digest.constData()), observed.sha256.size(),
                observed.sha256.begin());
    return QString::fromStdString(ztermy::ssh::sha256Fingerprint(observed));
}

[[nodiscard]] QVariantMap entryValue(const ztermy::ssh::KnownHostEntry &entry)
{
    const QByteArray key(reinterpret_cast<const char *>(entry.encodedKey.data()),
                         static_cast<qsizetype>(entry.encodedKey.size()));
    const QString host = text(entry.endpoint.host);
    return {
        {QStringLiteral("host"), host},
        {QStringLiteral("port"), entry.endpoint.port},
        {QStringLiteral("endpoint"),
         entry.endpoint.port == 22 ? host : QStringLiteral("[%1]:%2").arg(host).arg(entry.endpoint.port)},
        {QStringLiteral("algorithm"), QString::fromLatin1(ztermy::ssh::hostKeyAlgorithmName(entry.algorithm))},
        {QStringLiteral("algorithmToken"), QString::fromLatin1(ztermy::ssh::hostKeyAlgorithmToken(entry.algorithm))},
        {QStringLiteral("fingerprint"), fingerprint(entry)},
        {QStringLiteral("publicKey"),
         QStringLiteral("%1 %2").arg(QString::fromLatin1(ztermy::ssh::hostKeyAlgorithmToken(entry.algorithm)),
                                     QString::fromLatin1(key.toBase64()))},
    };
}

[[nodiscard]] QString importErrorText(const ztermy::ssh::OpenSshKnownHostsImportError error)
{
    switch (error)
    {
        case ztermy::ssh::OpenSshKnownHostsImportError::InvalidPath:
            return QStringLiteral("invalid-path");
        case ztermy::ssh::OpenSshKnownHostsImportError::IoError:
            return QStringLiteral("io-error");
        case ztermy::ssh::OpenSshKnownHostsImportError::TooLarge:
            return QStringLiteral("too-large");
    }
    return QStringLiteral("io-error");
}

struct ImportOperationResult final
{
    std::vector<ztermy::ssh::KnownHostEntry> entries;
    QVariantMap summary;
    QString error;
};

[[nodiscard]] ImportOperationResult importFiles(const QString &storePath, const QStringList &filePaths)
{
    const ztermy::ssh::KnownHostsStore store(storePath);
    std::vector<ztermy::ssh::KnownHostEntry> candidates;
    std::size_t parserDuplicates = 0;
    std::size_t unsupported = 0;
    std::size_t skipped = 0;
    std::size_t parsedLines = 0;
    QStringList importedSources;
    for (const QString &filePath : filePaths)
    {
        auto imported = ztermy::ssh::loadOpenSshKnownHosts(filePath);
        if (!imported)
        {
            return {.error = importErrorText(imported.error())};
        }
        importedSources.push_back(QFileInfo(filePath).fileName());
        unsupported += imported->unsupportedEntries;
        skipped += imported->skippedLines;
        parsedLines += imported->parsedLines;
        parserDuplicates += imported->duplicateEntries;
        candidates.insert(candidates.end(), std::make_move_iterator(imported->entries.begin()),
                          std::make_move_iterator(imported->entries.end()));
    }
    auto merged = store.mergeMissing(candidates);
    if (!merged)
    {
        return {.error = QStringLiteral("store-save")};
    }
    return {
        .entries = std::move(merged->entries),
        .summary =
            {
                {QStringLiteral("added"), static_cast<qulonglong>(merged->added)},
                {QStringLiteral("duplicates"), static_cast<qulonglong>(merged->duplicates + parserDuplicates)},
                {QStringLiteral("conflicts"), static_cast<qulonglong>(merged->conflicts)},
                {QStringLiteral("unsupported"), static_cast<qulonglong>(unsupported)},
                {QStringLiteral("skipped"), static_cast<qulonglong>(skipped)},
                {QStringLiteral("parsedLines"), static_cast<qulonglong>(parsedLines)},
                {QStringLiteral("sources"), importedSources},
            },
    };
}

} // namespace

namespace ztermy::ssh
{

KnownHostsController::KnownHostsController(QString storePath, QObject *parent)
    : QObject(parent), m_storePath(std::move(storePath))
{
    qRegisterMetaType<KnownHostEntries>();
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(15'000);
    QObject::connect(this, &KnownHostsController::operationFinished, this, &KnownHostsController::applyEntries,
                     Qt::QueuedConnection);
    refresh();
}

KnownHostsController::~KnownHostsController()
{
    m_worker.clear();
    m_worker.waitForDone();
}

QVariantList KnownHostsController::entries() const
{
    return m_entryValues;
}

int KnownHostsController::count() const noexcept
{
    return static_cast<int>(m_entries.size());
}

bool KnownHostsController::busy() const noexcept
{
    return m_busy;
}

QString KnownHostsController::operationError() const
{
    return m_operationError;
}

QVariantMap KnownHostsController::lastImportSummary() const
{
    return m_lastImportSummary;
}

QString KnownHostsController::defaultOpenSshPath() const
{
    return QDir::home().filePath(QStringLiteral(".ssh/known_hosts"));
}

bool KnownHostsController::beginOperation()
{
    if (m_busy)
    {
        return false;
    }
    m_busy = true;
    m_operationError.clear();
    emit stateChanged();
    return true;
}

bool KnownHostsController::refresh()
{
    if (!beginOperation())
    {
        return false;
    }
    const QPointer<KnownHostsController> self(this);
    const QString storePath = m_storePath;
    m_worker.start([self, storePath]() noexcept {
        try
        {
            const KnownHostsStore store(storePath);
            auto loaded = store.load();
            const QString error = loaded ? QString{} : QStringLiteral("store-load");
            KnownHostEntries entries = loaded ? std::move(*loaded) : KnownHostEntries{};
            if (self)
            {
                emit self->operationFinished(std::move(entries), error, {});
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("resource"), {});
            }
        }
    });
    return true;
}

bool KnownHostsController::importDefaultOpenSsh(const bool silent)
{
    QStringList paths;
    const QString primary = defaultOpenSshPath();
    if (QFileInfo::exists(primary))
    {
        paths.push_back(primary);
    }
    const QString legacy = QDir::home().filePath(QStringLiteral(".ssh/known_hosts2"));
    if (QFileInfo::exists(legacy))
    {
        paths.push_back(legacy);
    }
    const QString system = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                               .filePath(QStringLiteral("ssh/known_hosts"));
    if (QFileInfo::exists(system))
    {
        paths.push_back(system);
    }
    if (paths.isEmpty())
    {
        if (!silent)
        {
            m_operationError = tr("No OpenSSH known_hosts file was found for the current user.");
            m_lastImportSummary.clear();
            emit stateChanged();
        }
        return false;
    }
    if (!beginOperation())
    {
        return false;
    }
    const QPointer<KnownHostsController> self(this);
    const QString storePath = m_storePath;
    m_worker.start([self, storePath, paths, silent]() noexcept {
        try
        {
            ImportOperationResult result = importFiles(storePath, paths);
            if (self)
            {
                emit self->operationFinished(std::move(result.entries), result.error,
                                             silent ? QVariantMap{} : result.summary);
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("resource"), {});
            }
        }
    });
    return true;
}

bool KnownHostsController::importOpenSshFile(const QString &localFileUrl)
{
    const QUrl url(localFileUrl);
    const QString filePath = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (filePath.trimmed().isEmpty() || !beginOperation())
    {
        return false;
    }
    const QPointer<KnownHostsController> self(this);
    const QString storePath = m_storePath;
    m_worker.start([self, storePath, filePath]() noexcept {
        try
        {
            ImportOperationResult result = importFiles(storePath, {filePath});
            if (self)
            {
                emit self->operationFinished(std::move(result.entries), result.error, result.summary);
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("resource"), {});
            }
        }
    });
    return true;
}

bool KnownHostsController::removeEntry(const QString &host, const int port, const QString &algorithmToken)
{
    const QByteArray tokenUtf8 = algorithmToken.toUtf8();
    const auto algorithm =
        parseHostKeyAlgorithm(std::string_view(tokenUtf8.constData(), static_cast<std::size_t>(tokenUtf8.size())));
    if (host.trimmed().isEmpty() || port <= 0 || port > 65535 || !algorithm || !beginOperation())
    {
        return false;
    }
    const QByteArray hostUtf8 = host.toUtf8();
    const auto endpoint = std::make_shared<const SshEndpoint>(SshEndpoint{
        .host = std::string(hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size())),
        .port = static_cast<std::uint16_t>(port),
    });
    const QPointer<KnownHostsController> self(this);
    const QString storePath = m_storePath;
    m_worker.start([self, storePath, endpoint, algorithm = *algorithm]() noexcept {
        try
        {
            const KnownHostsStore store(storePath);
            auto removed = store.remove(*endpoint, algorithm);
            const QString error = removed ? QString{} : QStringLiteral("store-save");
            KnownHostEntries entries = removed ? std::move(*removed) : KnownHostEntries{};
            if (self)
            {
                emit self->operationFinished(std::move(entries), error, {});
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("resource"), {});
            }
        }
    });
    return true;
}

bool KnownHostsController::clearAll()
{
    if (!beginOperation())
    {
        return false;
    }
    const QPointer<KnownHostsController> self(this);
    const QString storePath = m_storePath;
    m_worker.start([self, storePath]() noexcept {
        try
        {
            const KnownHostsStore store(storePath);
            const QString error = store.clear() ? QString{} : QStringLiteral("store-save");
            if (self)
            {
                emit self->operationFinished({}, error, {});
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("resource"), {});
            }
        }
    });
    return true;
}

bool KnownHostsController::copyText(const QString &value) const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || value.isEmpty())
    {
        return false;
    }
    clipboard->setText(value);
    return true;
}

void KnownHostsController::applyEntries(KnownHostEntries entries, const QString &error, const QVariantMap &summary)
{
    m_busy = false;
    m_lastImportSummary = summary;
    if (error == QStringLiteral("store-load"))
    {
        m_operationError = tr("Unable to load the trusted host keys.");
    }
    else if (error == QStringLiteral("store-save"))
    {
        m_operationError = tr("Unable to save the trusted host keys.");
    }
    else if (error == QStringLiteral("not-found"))
    {
        m_operationError = tr("The trusted host key no longer exists.");
    }
    else if (error == QStringLiteral("too-large"))
    {
        m_operationError = tr("The selected OpenSSH known_hosts file is too large.");
    }
    else if (error == QStringLiteral("invalid-path") || error == QStringLiteral("io-error"))
    {
        m_operationError = tr("Unable to read the selected OpenSSH known_hosts file.");
    }
    else if (error == QStringLiteral("resource"))
    {
        m_operationError = tr("The known-host operation could not allocate the required resources.");
    }
    else
    {
        m_operationError.clear();
    }
    if (error.isEmpty())
    {
        std::ranges::sort(entries, [](const KnownHostEntry &left, const KnownHostEntry &right) {
            if (left.endpoint.host != right.endpoint.host)
            {
                return left.endpoint.host < right.endpoint.host;
            }
            if (left.endpoint.port != right.endpoint.port)
            {
                return left.endpoint.port < right.endpoint.port;
            }
            return left.algorithm < right.algorithm;
        });
        m_entries = std::move(entries);
        m_entryValues.clear();
        m_entryValues.reserve(static_cast<qsizetype>(m_entries.size()));
        for (const KnownHostEntry &entry : m_entries)
        {
            m_entryValues.push_back(entryValue(entry));
        }
        emit entriesChanged();
    }
    emit stateChanged();
}

} // namespace ztermy::ssh
