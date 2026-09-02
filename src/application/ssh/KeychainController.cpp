#include "application/ssh/KeychainController.h"

#include "core/security/CredentialVault.h"
#include "core/security/SensitiveByteArray.h"
#include "infrastructure/ssh/SshKeychainStore.h"

#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace
{

constexpr qint64 maximumPrivateKeyBytes = qint64{1024} * 1024;
constexpr qint64 maximumCertificateBytes = qint64{256} * 1024;

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString localPath(const QString &urlOrPath)
{
    const QUrl url(urlOrPath);
    return QDir::cleanPath(url.isLocalFile() ? url.toLocalFile() : urlOrPath);
}

[[nodiscard]] std::optional<ztermy::ssh::SshKeyType> keyType(const QString &token)
{
    const QByteArray bytes = token.trimmed().toLatin1().toLower();
    return ztermy::ssh::parseSshKeyType(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

[[nodiscard]] std::optional<ztermy::ssh::SshIdentityAuthentication> identityAuthentication(const QString &token)
{
    const QByteArray bytes = token.trimmed().toLatin1().toLower();
    return ztermy::ssh::parseSshIdentityAuthentication(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

[[nodiscard]] QString sshKeygenPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("ssh-keygen.exe"));
}

[[nodiscard]] std::optional<ztermy::ssh::SshKeyType> keyTypeFromPublicKey(const QByteArray &publicKey)
{
    const QByteArray algorithm = publicKey.trimmed().split(' ').value(0);
    if (algorithm == "ssh-ed25519" || algorithm == "ssh-ed25519-cert-v01@openssh.com")
    {
        return ztermy::ssh::SshKeyType::Ed25519;
    }
    if (algorithm == "ssh-rsa" || algorithm == "ssh-rsa-cert-v01@openssh.com")
    {
        return ztermy::ssh::SshKeyType::Rsa;
    }
    if (algorithm.startsWith("ecdsa-sha2-"))
    {
        return ztermy::ssh::SshKeyType::Ecdsa;
    }
    return std::nullopt;
}

[[nodiscard]] QByteArray derivePublicKey(const QString &privateKeyPath, const QString &passphrase, QString &error)
{
    const QString executable = sshKeygenPath();
    if (executable.isEmpty())
    {
        error = QStringLiteral("generator-unavailable");
        return {};
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments(
        {QStringLiteral("-y"), QStringLiteral("-P"), passphrase, QStringLiteral("-f"), privateKeyPath});
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(5'000) || !process.waitForFinished(30'000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        process.kill();
        process.waitForFinished(1'000);
        error = QStringLiteral("invalid-key");
        return {};
    }
    QByteArray output = process.readAllStandardOutput().trimmed();
    if (!keyTypeFromPublicKey(output))
    {
        error = QStringLiteral("unsupported-key");
        return {};
    }
    output.append('\n');
    return output;
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

[[nodiscard]] bool copyFile(const QString &source, const QString &destination)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 || input.size() > maximumPrivateKeyBytes)
    {
        return false;
    }
    return writeFile(destination, input.readAll());
}

[[nodiscard]] bool credentialStored(const std::vector<ztermy::security::CredentialKey> &keys,
                                    const std::string &reference, const ztermy::security::CredentialKind kind)
{
    return std::ranges::any_of(keys, [&reference, kind](const ztermy::security::CredentialKey &key) {
        return key.profileId == reference && key.kind == kind;
    });
}

[[nodiscard]] bool credentialStored(const ztermy::security::CredentialVault &vault, const std::string &reference,
                                    const ztermy::security::CredentialKind kind)
{
    const auto keys = vault.listKeys();
    return keys && credentialStored(*keys, reference, kind);
}

[[nodiscard]] bool pathIsManagedBy(const QString &path, const QString &managedDirectory)
{
    if (path.isEmpty() || managedDirectory.isEmpty())
    {
        return false;
    }
    const QDir directory(QFileInfo(managedDirectory).absoluteFilePath());
    const QString relative = directory.relativeFilePath(QFileInfo(path).absoluteFilePath());
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))
           && !relative.startsWith(QStringLiteral("..\\")) && !QDir::isAbsolutePath(relative);
}

void removeManagedKeyFiles(const ztermy::ssh::SshKeyRecord &record, const QString &managedDirectory)
{
    if (record.source == ztermy::ssh::SshKeySource::Reference)
    {
        return;
    }
    for (const std::string *path : {&record.privateKeyPath, &record.publicKeyPath, &record.certificatePath})
    {
        const QString local = text(*path);
        if (pathIsManagedBy(local, managedDirectory))
        {
            QFile::remove(local);
        }
    }
}

[[nodiscard]] QStringList profileReferences(const std::vector<ztermy::ssh::SshProfile> &profiles,
                                            const std::string &identityId)
{
    QStringList result;
    for (const ztermy::ssh::SshProfile &profile : profiles)
    {
        if (profile.identityReference && *profile.identityReference == identityId)
        {
            result.push_back(text(profile.name));
        }
    }
    return result;
}

[[nodiscard]] QString errorText(const QString &token)
{
    if (token == QStringLiteral("generator-unavailable"))
    {
        return QCoreApplication::translate("KeychainController", "Windows OpenSSH ssh-keygen is not available.");
    }
    if (token == QStringLiteral("invalid-key"))
    {
        return QCoreApplication::translate("KeychainController",
                                           "The private key could not be read. Check its format and passphrase.");
    }
    if (token == QStringLiteral("invalid-certificate"))
    {
        return QCoreApplication::translate("KeychainController",
                                           "The selected OpenSSH certificate is invalid or unsupported.");
    }
    if (token == QStringLiteral("referenced"))
    {
        return QCoreApplication::translate("KeychainController",
                                           "This item is still referenced and cannot be deleted.");
    }
    if (token == QStringLiteral("credential"))
    {
        return QCoreApplication::translate("KeychainController",
                                           "The credential could not be saved in the selected storage.");
    }
    if (token == QStringLiteral("invalid"))
    {
        return QCoreApplication::translate("KeychainController", "Complete the required keychain fields.");
    }
    return QCoreApplication::translate("KeychainController", "The keychain could not be saved.");
}

struct KeyMaterialResult final
{
    ztermy::ssh::SshKeyRecord record;
    QByteArray passphrase;
    QString error;
    QStringList createdFiles;
};

[[nodiscard]] KeyMaterialResult generateKeyMaterial(const QString &managedDirectory, const QString &label,
                                                    const ztermy::ssh::SshKeyType type, const int bits,
                                                    const QString &passphrase)
{
    KeyMaterialResult result;
    const QString executable = sshKeygenPath();
    if (executable.isEmpty() || !QDir().mkpath(managedDirectory))
    {
        result.error = executable.isEmpty() ? QStringLiteral("generator-unavailable") : QStringLiteral("store");
        return result;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString privatePath = QDir(managedDirectory).filePath(id);
    QStringList arguments{
        QStringLiteral("-q"), QStringLiteral("-t"), QString::fromLatin1(ztermy::ssh::sshKeyTypeToken(type)),
        QStringLiteral("-f"), privatePath,          QStringLiteral("-N"),
        passphrase,           QStringLiteral("-C"), QStringLiteral("ztermy")};
    if (type != ztermy::ssh::SshKeyType::Ed25519 && bits > 0)
    {
        arguments.insert(3, QStringLiteral("-b"));
        arguments.insert(4, QString::number(bits));
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(5'000) || !process.waitForFinished(30'000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        process.kill();
        process.waitForFinished(1'000);
        QFile::remove(privatePath);
        QFile::remove(privatePath + QStringLiteral(".pub"));
        result.error = QStringLiteral("invalid-key");
        return result;
    }
    result.record = {
        .id = text(id),
        .label = text(label.trimmed()),
        .type = type,
        .kind = ztermy::ssh::SshKeyKind::Key,
        .source = ztermy::ssh::SshKeySource::Generated,
        .privateKeyPath = text(QDir::toNativeSeparators(privatePath)),
        .publicKeyPath = text(QDir::toNativeSeparators(privatePath + QStringLiteral(".pub"))),
        .createdUtcMs = QDateTime::currentMSecsSinceEpoch(),
    };
    result.passphrase = passphrase.toUtf8();
    result.createdFiles = {privatePath, privatePath + QStringLiteral(".pub")};
    return result;
}

[[nodiscard]] KeyMaterialResult importKeyMaterial(const QString &managedDirectory, const QString &sourcePath,
                                                  const QString &certificatePath, const QString &label,
                                                  const bool copyIntoKeychain, const QString &passphrase)
{
    KeyMaterialResult result;
    const QFileInfo source(sourcePath);
    if (!source.isFile() || source.size() <= 0 || source.size() > maximumPrivateKeyBytes)
    {
        result.error = QStringLiteral("invalid-key");
        return result;
    }
    QString deriveError;
    const QByteArray publicKey = derivePublicKey(source.absoluteFilePath(), passphrase, deriveError);
    const auto type = keyTypeFromPublicKey(publicKey);
    if (!type)
    {
        result.error = deriveError.isEmpty() ? QStringLiteral("unsupported-key") : deriveError;
        return result;
    }
    QByteArray certificate;
    if (!certificatePath.isEmpty())
    {
        QFile file(certificatePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maximumCertificateBytes)
        {
            result.error = QStringLiteral("invalid-certificate");
            return result;
        }
        certificate = file.readAll().trimmed();
        const auto certificateType = keyTypeFromPublicKey(certificate);
        if (!certificateType || *certificateType != *type
            || !certificate.split(' ').value(0).contains("-cert-v01@openssh.com"))
        {
            result.error = QStringLiteral("invalid-certificate");
            return result;
        }
        certificate.append('\n');
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString privateDestination = source.absoluteFilePath();
    QString publicDestination;
    QString certificateDestination = certificatePath;
    if (copyIntoKeychain)
    {
        if (!QDir().mkpath(managedDirectory))
        {
            result.error = QStringLiteral("store");
            return result;
        }
        privateDestination = QDir(managedDirectory).filePath(id);
        publicDestination = privateDestination + QStringLiteral(".pub");
        if (!copyFile(source.absoluteFilePath(), privateDestination) || !writeFile(publicDestination, publicKey))
        {
            QFile::remove(privateDestination);
            QFile::remove(publicDestination);
            result.error = QStringLiteral("store");
            return result;
        }
        result.createdFiles = {privateDestination, publicDestination};
        if (!certificate.isEmpty())
        {
            certificateDestination = privateDestination + QStringLiteral("-cert.pub");
            if (!writeFile(certificateDestination, certificate))
            {
                for (const QString &path : result.createdFiles)
                {
                    QFile::remove(path);
                }
                result.createdFiles.clear();
                result.error = QStringLiteral("store");
                return result;
            }
            result.createdFiles.push_back(certificateDestination);
        }
    }
    else
    {
        publicDestination = source.absoluteFilePath() + QStringLiteral(".pub");
        if (!QFileInfo::exists(publicDestination))
        {
            publicDestination.clear();
        }
    }
    result.record = {
        .id = text(id),
        .label = text(label.trimmed()),
        .type = *type,
        .kind = certificate.isEmpty() ? ztermy::ssh::SshKeyKind::Key : ztermy::ssh::SshKeyKind::Certificate,
        .source = copyIntoKeychain ? ztermy::ssh::SshKeySource::Imported : ztermy::ssh::SshKeySource::Reference,
        .privateKeyPath = text(QDir::toNativeSeparators(privateDestination)),
        .publicKeyPath = text(QDir::toNativeSeparators(publicDestination)),
        .certificatePath = text(QDir::toNativeSeparators(certificateDestination)),
        .createdUtcMs = QDateTime::currentMSecsSinceEpoch(),
    };
    result.passphrase = passphrase.toUtf8();
    return result;
}

} // namespace

namespace ztermy::ssh
{

KeychainController::KeychainController(QString storePath, QString managedKeyDirectory, SshKeychainCatalog catalog,
                                       security::CredentialVaultCoordinator *vaults, QObject *parent)
    : QObject(parent),
      m_storePath(std::move(storePath)),
      m_managedKeyDirectory(std::move(managedKeyDirectory)),
      m_catalog(std::move(catalog)),
      m_vaults(vaults)
{
    qRegisterMetaType<SshKeychainCatalog>();
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(15'000);
    QObject::connect(this, &KeychainController::operationFinished, this, &KeychainController::applyOperation,
                     Qt::QueuedConnection);
    rebuildProjection();
}

KeychainController::~KeychainController()
{
    m_worker.clear();
    m_worker.waitForDone();
}

QVariantList KeychainController::keys() const
{
    return m_keyValues;
}

QVariantList KeychainController::identities() const
{
    return m_identityValues;
}

bool KeychainController::busy() const noexcept
{
    return m_busy;
}

QString KeychainController::operationError() const
{
    return m_operationError;
}

bool KeychainController::keyGeneratorAvailable() const
{
    return !sshKeygenPath().isEmpty();
}

const SshKeychainCatalog &KeychainController::catalog() const noexcept
{
    return m_catalog;
}

void KeychainController::setProfiles(const std::vector<SshProfile> &profiles)
{
    m_profiles = profiles;
    rebuildProjection();
    emit catalogChanged();
}

bool KeychainController::beginOperation()
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

void KeychainController::startTask(std::function<void()> task)
{
    const QPointer<KeychainController> self(this);
    m_worker.start([self, task = std::move(task)]() mutable noexcept {
        try
        {
            task();
        }
        catch (...)
        {
            if (self)
            {
                emit self->operationFinished({}, QStringLiteral("store"), {});
            }
        }
    });
}

bool KeychainController::refresh()
{
    if (!beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString path = m_storePath;
    startTask([self, path] {
        auto loaded = SshKeychainStore(path).load();
        if (self)
        {
            emit self->operationFinished(loaded ? std::move(*loaded) : SshKeychainCatalog{},
                                         loaded ? QString{} : QStringLiteral("store"), {});
        }
    });
    return true;
}

bool KeychainController::generateKey(const QString &label, const QString &typeToken, const int bits,
                                     const QString &passphrase)
{
    const auto type = keyType(typeToken);
    if (label.trimmed().isEmpty() || !type || !beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    const QString managedDirectory = m_managedKeyDirectory;
    const SshKeychainCatalog current = m_catalog;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    // NOLINTNEXTLINE(bugprone-exception-escape): startTask catches exceptions at the worker boundary.
    startTask([self, storePath, managedDirectory, current, label, type = *type, bits, passphrase, vault]() mutable {
        KeyMaterialResult material = generateKeyMaterial(managedDirectory, label, type, bits, passphrase);
        QString error = material.error;
        SshKeychainCatalog candidate = current;
        if (error.isEmpty())
        {
            candidate.keys.push_back(material.record);
            if (!SshKeychainStore(storePath).save(candidate))
            {
                error = QStringLiteral("store");
            }
            else if (!material.passphrase.isEmpty()
                     && (vault == nullptr
                         || !vault->store(
                             {.profileId = material.record.id, .kind = security::CredentialKind::PrivateKeyPassphrase},
                             security::SensitiveByteArray(std::move(material.passphrase)))))
            {
                const auto rolledBack = SshKeychainStore(storePath).save(current);
                error = rolledBack ? QStringLiteral("credential") : QStringLiteral("store");
            }
        }
        if (!error.isEmpty())
        {
            for (const QString &path : material.createdFiles)
            {
                QFile::remove(path);
            }
            candidate = current;
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::importKey(const QString &localFileUrl, const QString &label, const bool copyIntoKeychain,
                                   const QString &passphrase)
{
    const QString path = localPath(localFileUrl);
    if (path.isEmpty() || label.trimmed().isEmpty() || !beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    const QString managedDirectory = m_managedKeyDirectory;
    const SshKeychainCatalog current = m_catalog;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    // NOLINTNEXTLINE(bugprone-exception-escape): startTask catches exceptions at the worker boundary.
    startTask([self, storePath, managedDirectory, current, path, label, copyIntoKeychain, passphrase, vault]() mutable {
        KeyMaterialResult material = importKeyMaterial(managedDirectory, path, {}, label, copyIntoKeychain, passphrase);
        QString error = material.error;
        SshKeychainCatalog candidate = current;
        if (error.isEmpty())
        {
            candidate.keys.push_back(material.record);
            if (!SshKeychainStore(storePath).save(candidate))
            {
                error = QStringLiteral("store");
            }
            else if (!material.passphrase.isEmpty()
                     && (vault == nullptr
                         || !vault->store(
                             {.profileId = material.record.id, .kind = security::CredentialKind::PrivateKeyPassphrase},
                             security::SensitiveByteArray(std::move(material.passphrase)))))
            {
                const auto rolledBack = SshKeychainStore(storePath).save(current);
                error = rolledBack ? QStringLiteral("credential") : QStringLiteral("store");
            }
        }
        if (!error.isEmpty())
        {
            for (const QString &created : material.createdFiles)
            {
                QFile::remove(created);
            }
            candidate = current;
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::importCertificate(const QString &privateKeyFileUrl, const QString &certificateFileUrl,
                                           const QString &label, const bool copyIntoKeychain, const QString &passphrase)
{
    const QString privatePath = localPath(privateKeyFileUrl);
    const QString certificatePath = localPath(certificateFileUrl);
    if (privatePath.isEmpty() || certificatePath.isEmpty() || label.trimmed().isEmpty() || !beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    const QString managedDirectory = m_managedKeyDirectory;
    const SshKeychainCatalog current = m_catalog;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    // NOLINTNEXTLINE(bugprone-exception-escape): startTask catches exceptions at the worker boundary.
    startTask([self, storePath, managedDirectory, current, privatePath, certificatePath, label, copyIntoKeychain,
               passphrase, vault]() mutable {
        KeyMaterialResult material =
            importKeyMaterial(managedDirectory, privatePath, certificatePath, label, copyIntoKeychain, passphrase);
        QString error = material.error;
        SshKeychainCatalog candidate = current;
        if (error.isEmpty())
        {
            candidate.keys.push_back(material.record);
            if (!SshKeychainStore(storePath).save(candidate))
            {
                error = QStringLiteral("store");
            }
            else if (!material.passphrase.isEmpty()
                     && (vault == nullptr
                         || !vault->store(
                             {.profileId = material.record.id, .kind = security::CredentialKind::PrivateKeyPassphrase},
                             security::SensitiveByteArray(std::move(material.passphrase)))))
            {
                const auto rolledBack = SshKeychainStore(storePath).save(current);
                error = rolledBack ? QStringLiteral("credential") : QStringLiteral("store");
            }
        }
        if (!error.isEmpty())
        {
            for (const QString &created : material.createdFiles)
            {
                QFile::remove(created);
            }
            candidate = current;
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::saveIdentity(const QString &id, const QString &label, const QString &username,
                                      const QString &authenticationToken, const QString &keyId,
                                      const bool credentialRequired, const QString &secret, const bool rememberSecret)
{
    const auto authentication = identityAuthentication(authenticationToken);
    const QString normalizedId =
        id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    if (!authentication || label.trimmed().isEmpty() || username.trimmed().isEmpty())
    {
        return false;
    }
    const auto existing = std::ranges::find(m_catalog.identities, text(normalizedId), &SshIdentity::id);
    const bool keyAuthentication = *authentication == SshIdentityAuthentication::PrivateKey
                                   || *authentication == SshIdentityAuthentication::Certificate;
    std::optional<std::string> credentialReference;
    if (rememberSecret
        && (!secret.isEmpty() || (existing != m_catalog.identities.end() && existing->credentialReference)))
    {
        credentialReference = !secret.isEmpty() ? text(normalizedId) : existing->credentialReference;
    }
    else if (rememberSecret && keyAuthentication && !keyId.trimmed().isEmpty() && m_vaults != nullptr
             && credentialStored(m_vaults->active(), text(keyId.trimmed()),
                                 security::CredentialKind::PrivateKeyPassphrase))
    {
        credentialReference = text(keyId.trimmed());
    }
    SshIdentity replacement{
        .id = text(normalizedId),
        .label = text(label.trimmed()),
        .username = text(username.trimmed()),
        .authentication = *authentication,
        .keyId = keyAuthentication ? std::optional{text(keyId.trimmed())} : std::nullopt,
        .credentialRequired = *authentication == SshIdentityAuthentication::Password || credentialRequired,
        .credentialReference = credentialReference,
        .createdUtcMs =
            existing == m_catalog.identities.end() ? QDateTime::currentMSecsSinceEpoch() : existing->createdUtcMs,
    };
    if (!validSshIdentity(replacement) || !beginOperation())
    {
        return false;
    }
    SshKeychainCatalog current = m_catalog;
    SshKeychainCatalog candidate = current;
    const auto target = std::ranges::find(candidate.identities, replacement.id, &SshIdentity::id);
    if (target == candidate.identities.end())
    {
        candidate.identities.push_back(replacement);
    }
    else
    {
        *target = replacement;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    const QByteArray secretBytes = secret.toUtf8();
    startTask([self, storePath, current = std::move(current), candidate = std::move(candidate), replacement,
               rememberSecret, secretBytes, vault]() mutable {
        QString error;
        if (!SshKeychainStore(storePath).save(candidate))
        {
            error = QStringLiteral("store");
        }
        else if (rememberSecret && !secretBytes.isEmpty()
                 && (vault == nullptr
                     || !vault->store({.profileId = replacement.id,
                                       .kind = replacement.authentication == SshIdentityAuthentication::Password
                                                   ? security::CredentialKind::Password
                                                   : security::CredentialKind::PrivateKeyPassphrase},
                                      security::SensitiveByteArray(QByteArray(secretBytes)))))
        {
            const auto rolledBack = SshKeychainStore(storePath).save(current);
            candidate = current;
            error = rolledBack ? QStringLiteral("credential") : QStringLiteral("store");
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::removeKey(const QString &id)
{
    const std::string recordId = text(id.trimmed());
    if (recordId.empty() || std::ranges::any_of(m_catalog.identities, [&recordId](const SshIdentity &identity) {
            return identity.keyId && *identity.keyId == recordId;
        }))
    {
        m_operationError = errorText(QStringLiteral("referenced"));
        emit stateChanged();
        return false;
    }
    SshKeychainCatalog candidate = m_catalog;
    const auto existing = std::ranges::find(candidate.keys, recordId, &SshKeyRecord::id);
    if (existing == candidate.keys.end())
    {
        return false;
    }
    const auto removed = std::make_shared<const SshKeyRecord>(*existing);
    const auto before = candidate.keys.size();
    std::erase_if(candidate.keys, [&recordId](const SshKeyRecord &record) {
        return record.id == recordId;
    });
    if (candidate.keys.size() == before || !beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    const QString managedDirectory = m_managedKeyDirectory;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    startTask([self, storePath, managedDirectory, candidate = std::move(candidate), removed, vault]() mutable {
        const QString error = SshKeychainStore(storePath).save(candidate) ? QString{} : QStringLiteral("store");
        if (error.isEmpty())
        {
            removeManagedKeyFiles(*removed, managedDirectory);
            if (vault != nullptr)
            {
                [[maybe_unused]] const auto removal =
                    vault->remove({.profileId = removed->id, .kind = security::CredentialKind::PrivateKeyPassphrase});
            }
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::removeIdentity(const QString &id)
{
    const std::string identityId = text(id.trimmed());
    if (identityId.empty() || !profileReferences(m_profiles, identityId).isEmpty())
    {
        m_operationError = errorText(QStringLiteral("referenced"));
        emit stateChanged();
        return false;
    }
    SshKeychainCatalog candidate = m_catalog;
    const auto existing = std::ranges::find(candidate.identities, identityId, &SshIdentity::id);
    if (existing == candidate.identities.end())
    {
        return false;
    }
    const auto removed = std::make_shared<const SshIdentity>(*existing);
    const auto before = candidate.identities.size();
    std::erase_if(candidate.identities, [&identityId](const SshIdentity &identity) {
        return identity.id == identityId;
    });
    if (candidate.identities.size() == before || !beginOperation())
    {
        return false;
    }
    const QPointer<KeychainController> self(this);
    const QString storePath = m_storePath;
    security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    startTask([self, storePath, candidate = std::move(candidate), removed, vault]() mutable {
        const QString error = SshKeychainStore(storePath).save(candidate) ? QString{} : QStringLiteral("store");
        if (error.isEmpty() && vault != nullptr && removed->credentialReference
            && *removed->credentialReference == removed->id)
        {
            const security::CredentialKind kind = removed->authentication == SshIdentityAuthentication::Password
                                                      ? security::CredentialKind::Password
                                                      : security::CredentialKind::PrivateKeyPassphrase;
            [[maybe_unused]] const auto removal = vault->remove({.profileId = removed->id, .kind = kind});
        }
        if (self)
        {
            emit self->operationFinished(std::move(candidate), error, {});
        }
    });
    return true;
}

bool KeychainController::copyText(const QString &value) const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || value.isEmpty())
    {
        return false;
    }
    clipboard->setText(value);
    return true;
}

void KeychainController::applyOperation(SshKeychainCatalog catalog, const QString &error, const QVariantMap &summary)
{
    static_cast<void>(summary);
    if (error.isEmpty())
    {
        m_catalog = std::move(catalog);
        m_operationError.clear();
        rebuildProjection();
        emit catalogChanged();
    }
    else
    {
        m_operationError = errorText(error);
    }
    m_busy = false;
    emit stateChanged();
}

void KeychainController::rebuildProjection()
{
    const security::CredentialVault *vault = m_vaults == nullptr ? nullptr : &m_vaults->active();
    std::vector<security::CredentialKey> credentialKeys;
    if (vault != nullptr)
    {
        if (auto listedKeys = vault->listKeys())
        {
            credentialKeys = std::move(*listedKeys);
        }
    }
    m_keyValues.clear();
    m_keyValues.reserve(static_cast<qsizetype>(m_catalog.keys.size()));
    for (const SshKeyRecord &record : m_catalog.keys)
    {
        const QStringList identities = [&record, this] {
            QStringList result;
            for (const SshIdentity &identity : m_catalog.identities)
            {
                if (identity.keyId && *identity.keyId == record.id)
                {
                    result.push_back(text(identity.label));
                }
            }
            return result;
        }();
        m_keyValues.push_back(QVariantMap{
            {QStringLiteral("id"), text(record.id)},
            {QStringLiteral("label"), text(record.label)},
            {QStringLiteral("type"), QString::fromLatin1(sshKeyTypeToken(record.type)).toUpper()},
            {QStringLiteral("kind"), QString::fromLatin1(sshKeyKindToken(record.kind))},
            {QStringLiteral("source"), QString::fromLatin1(sshKeySourceToken(record.source))},
            {QStringLiteral("privateKeyPath"), text(record.privateKeyPath)},
            {QStringLiteral("publicKeyPath"), text(record.publicKeyPath)},
            {QStringLiteral("certificatePath"), text(record.certificatePath)},
            {QStringLiteral("credentialStored"),
             vault != nullptr
                 && credentialStored(credentialKeys, record.id, security::CredentialKind::PrivateKeyPassphrase)},
            {QStringLiteral("identityNames"), identities},
            {QStringLiteral("referenceCount"), identities.size()},
        });
    }
    m_identityValues.clear();
    m_identityValues.reserve(static_cast<qsizetype>(m_catalog.identities.size()));
    for (const SshIdentity &identity : m_catalog.identities)
    {
        const QStringList profiles = profileReferences(m_profiles, identity.id);
        m_identityValues.push_back(QVariantMap{
            {QStringLiteral("id"), text(identity.id)},
            {QStringLiteral("label"), text(identity.label)},
            {QStringLiteral("username"), text(identity.username)},
            {QStringLiteral("authentication"),
             QString::fromLatin1(sshIdentityAuthenticationToken(identity.authentication))},
            {QStringLiteral("keyId"), identity.keyId ? text(*identity.keyId) : QString{}},
            {QStringLiteral("credentialRequired"), identity.credentialRequired},
            {QStringLiteral("credentialStored"),
             vault != nullptr && identity.credentialReference
                 && credentialStored(credentialKeys, *identity.credentialReference,
                                     identity.authentication == SshIdentityAuthentication::Password
                                         ? security::CredentialKind::Password
                                         : security::CredentialKind::PrivateKeyPassphrase)},
            {QStringLiteral("profileNames"), profiles},
            {QStringLiteral("referenceCount"), profiles.size()},
        });
    }
}

} // namespace ztermy::ssh
