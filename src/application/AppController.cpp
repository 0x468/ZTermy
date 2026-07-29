#include "application/AppController.h"

#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <system_error>

Q_LOGGING_CATEGORY(appControllerLog, "ztermy.application.controller")

namespace
{

[[nodiscard]] QString applicationDataFile(const QString &fileName)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName);
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString utf8QString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

namespace ztermy
{

AppController::AppController(QObject *parent)
    : AppController(applicationDataFile(QStringLiteral("profiles.json")), parent)
{
}

AppController::AppController(QString profileStorePath, QObject *parent)
    : QObject(parent), m_profileStore(std::move(profileStorePath))
{
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::snapshotReady, this,
                     [this](const terminal::TerminalSnapshotPtr &snapshot) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::statusChanged, this,
                     [this](const QString &status) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setStatusText(status);
                         }
                     });
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::clipboardTextReady, this,
                     [this](const QString &text) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });

    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::snapshotReady, this,
                     [this](const terminal::TerminalSnapshotPtr &snapshot) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Ssh)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::statusChanged, this, [this](const QString &status) {
        if (m_terminal != nullptr && m_activeSession == ActiveSession::Ssh)
        {
            m_terminal->setStatusText(status);
        }
    });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::runningChanged, this, [this](const bool running) {
        if (m_activeSession == ActiveSession::Ssh || running)
        {
            emit sshActiveChanged();
        }
    });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this](const QString &algorithm, const QString &fingerprint) {
                         setHostKeyPrompt(algorithm, fingerprint, false);
                     });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this](const QString &algorithm, const QString &fingerprint) {
                         setHostKeyPrompt(algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(QStringLiteral("SSH host key changed; connection blocked"));
                         }
                     });
    loadHostProfiles();
}

AppController::~AppController()
{
    shutdown();
}

void AppController::attachTerminal(ui::TerminalItem *terminal)
{
    if (m_terminal == terminal)
    {
        return;
    }
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
    }
    m_terminal = terminal;
    connectTerminalSignals();
}

void AppController::shutdown() noexcept
{
    clearHostKeyPrompt();
    m_sshSession.stop();
    m_localSession.stop();
    m_activeSession = ActiveSession::None;
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
        m_terminal->setSnapshot({});
        m_terminal = nullptr;
    }
}

bool AppController::sshActive() const noexcept
{
    return m_activeSession == ActiveSession::Ssh;
}

bool AppController::hostKeyPromptVisible() const noexcept
{
    return m_hostKeyPromptVisible;
}

QString AppController::hostKeyAlgorithm() const
{
    return m_hostKeyAlgorithm;
}

QString AppController::hostKeyFingerprint() const
{
    return m_hostKeyFingerprint;
}

bool AppController::hostKeyChangedWarning() const noexcept
{
    return m_hostKeyChangedWarning;
}

QString AppController::defaultPrivateKeyPath() const
{
    return QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                        .filePath(QStringLiteral(".ssh/id_ed25519")));
}

QVariantList AppController::hostProfiles() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_profiles.size()));
    for (const ssh::SshProfile &profile : m_profiles)
    {
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(profile.id)},
            {QStringLiteral("name"), utf8QString(profile.name)},
            {QStringLiteral("host"), utf8QString(profile.host)},
            {QStringLiteral("port"), profile.port},
            {QStringLiteral("username"), utf8QString(profile.username)},
            {QStringLiteral("authentication"), profile.authentication == ssh::SshAuthenticationMethod::PrivateKey
                                                   ? QStringLiteral("private-key")
                                                   : QStringLiteral("password")},
            {QStringLiteral("privateKeyPath"), utf8QString(profile.privateKeyPath)},
            {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
        });
    }
    return result;
}

void AppController::startLocalTerminal()
{
    clearHostKeyPrompt();
    m_sshSession.stop();
    m_localSession.stop();
    m_activeSession = ActiveSession::Local;
    emit sshActiveChanged();

    if (m_terminal != nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(QStringLiteral("Starting local terminal..."));
    }
    const std::error_code error = m_localSession.start({.columns = 100, .rows = 30});
    if (error && m_terminal != nullptr)
    {
        m_terminal->setStatusText(
            QStringLiteral("Unable to start local terminal: %1").arg(QString::fromStdString(error.message())));
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath, const QString &passphrase)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || privateKeyPath.trimmed().isEmpty() || port <= 0
        || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Complete the SSH host, port, username, and private-key fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = privateKeyPath.trimmed(),
        .secret = security::SensitiveByteArray(passphrase.toUtf8()),
        .knownHostsPath = applicationDataFile(QStringLiteral("known_hosts.json")),
    };
    return startSshConnection(std::move(request));
}

bool AppController::connectPassword(const QString &host, const int port, const QString &username,
                                    const QString &password)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || password.isEmpty() || port <= 0 || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Complete the SSH host, port, username, and password fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::Password,
        .privateKeyPath = {},
        .secret = security::SensitiveByteArray(password.toUtf8()),
        .knownHostsPath = applicationDataFile(QStringLiteral("known_hosts.json")),
    };
    return startSshConnection(std::move(request));
}

bool AppController::startSshConnection(ssh::SshConnectionRequest request)
{
    clearHostKeyPrompt();
    m_localSession.stop();
    m_sshSession.stop();
    m_activeSession = ActiveSession::Ssh;
    emit sshActiveChanged();

    if (m_terminal != nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(QStringLiteral("Starting SSH connection..."));
    }

    const std::error_code error = m_sshSession.start(std::move(request), {.columns = 100, .rows = 30});
    if (error)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Unable to start SSH connection"));
        }
        return false;
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return true;
}

bool AppController::saveHostProfile(const QString &id, const QString &name, const QString &host, const int port,
                                    const QString &username, const QString &authentication,
                                    const QString &privateKeyPath, const bool privateKeyPassphraseRequired)
{
    const QString normalizedName = name.trimmed();
    const QString normalizedHost = host.trimmed();
    const QString normalizedUsername = username.trimmed();
    const QString normalizedPrivateKeyPath = privateKeyPath.trimmed();
    const QString profileId = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    const std::optional<ssh::SshAuthenticationMethod> authenticationMethod =
        authentication == QStringLiteral("private-key") ? std::optional{ssh::SshAuthenticationMethod::PrivateKey}
        : authentication == QStringLiteral("password")  ? std::optional{ssh::SshAuthenticationMethod::Password}
                                                        : std::nullopt;
    if (!authenticationMethod)
    {
        return false;
    }

    const ssh::SshProfile profile{
        .id = utf8String(profileId),
        .name = utf8String(normalizedName),
        .host = utf8String(normalizedHost),
        .port = port > 0 && port <= 65535 ? static_cast<std::uint16_t>(port) : std::uint16_t{0},
        .username = utf8String(normalizedUsername),
        .authentication = *authenticationMethod,
        .privateKeyPath = *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey
                              ? utf8String(normalizedPrivateKeyPath)
                              : std::string{},
        .privateKeyPassphraseRequired =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired,
    };
    if (!ssh::validSshProfile(profile))
    {
        return false;
    }

    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto existing = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    if (existing == updated.end())
    {
        updated.push_back(profile);
    }
    else
    {
        *existing = profile;
    }

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::deleteHostProfile(const QString &id)
{
    const std::string profileId = utf8String(id);
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, profileId, &ssh::SshProfile::id);
    if (profile == updated.end())
    {
        return false;
    }
    updated.erase(profile);

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles after deletion";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::connectHostProfile(const QString &id, const QString &secret)
{
    const std::string profileId = utf8String(id);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    if (profile->authentication == ssh::SshAuthenticationMethod::Password)
    {
        return connectPassword(utf8QString(profile->host), profile->port, utf8QString(profile->username), secret);
    }
    if (profile->privateKeyPassphraseRequired && secret.isEmpty())
    {
        return false;
    }
    return connectPrivateKey(utf8QString(profile->host), profile->port, utf8QString(profile->username),
                             utf8QString(profile->privateKeyPath), secret);
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    clearHostKeyPrompt();
    m_sshSession.confirmHostKey(remember);
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    clearHostKeyPrompt();
    m_sshSession.rejectHostKey();
}

void AppController::connectTerminalSignals()
{
    if (m_terminal == nullptr)
    {
        return;
    }
    QObject::connect(m_terminal, &ui::TerminalItem::inputGenerated, this, &AppController::queueInput);
    QObject::connect(m_terminal, &ui::TerminalItem::pasteRequested, this, &AppController::queuePaste);
    QObject::connect(m_terminal, &ui::TerminalItem::sizeRequested, this, &AppController::requestResize);
    QObject::connect(m_terminal, &ui::TerminalItem::scrollRequested, &m_localSession,
                     &terminal::LocalTerminalSession::requestScroll);
    QObject::connect(m_terminal, &ui::TerminalItem::selectionRequested, &m_localSession,
                     &terminal::LocalTerminalSession::requestSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::clearSelectionRequested, &m_localSession,
                     &terminal::LocalTerminalSession::clearSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::copyRequested, &m_localSession,
                     &terminal::LocalTerminalSession::copySelection);
}

void AppController::queueInput(const QByteArray &bytes)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.queueInput(bytes);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.queueInput(bytes);
    }
}

void AppController::queuePaste(const QByteArray &bytes)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.queueInput(bytes);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.queuePaste(bytes);
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

void AppController::setHostKeyPrompt(QString algorithm, QString fingerprint, const bool changed)
{
    m_hostKeyAlgorithm = std::move(algorithm);
    m_hostKeyFingerprint = std::move(fingerprint);
    m_hostKeyPromptVisible = true;
    m_hostKeyChangedWarning = changed;
    emit hostKeyPromptChanged();
}

void AppController::clearHostKeyPrompt()
{
    if (!m_hostKeyPromptVisible && m_hostKeyAlgorithm.isEmpty() && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyAlgorithm.clear();
    m_hostKeyFingerprint.clear();
    emit hostKeyPromptChanged();
}

void AppController::loadHostProfiles()
{
    auto profiles = m_profileStore.load();
    if (!profiles)
    {
        qCWarning(appControllerLog) << "Unable to load SSH profiles; starting with an empty profile list";
        return;
    }
    m_profiles = std::move(*profiles);
}

} // namespace ztermy
