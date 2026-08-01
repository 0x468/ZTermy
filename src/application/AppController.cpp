#include "application/AppController.h"

#include "domain/ssh/SshTarget.h"
#include "infrastructure/security/InMemoryCredentialVault.h"
#include "ui/terminal/TerminalItem.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <functional>
#include <optional>
#include <system_error>

Q_LOGGING_CATEGORY(appControllerLog, "ztermy.application.controller")

namespace
{

[[nodiscard]] QString applicationDataFile(const QString &fileName)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName);
}

[[nodiscard]] QString siblingKnownHostsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("known_hosts.json"));
}

[[nodiscard]] QString siblingSettingsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("settings.json"));
}

[[nodiscard]] QString siblingCredentialsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("credentials.zvlt"));
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

[[nodiscard]] bool profileHasStoredCredential(const ztermy::ssh::SshProfile &profile,
                                              const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    if (!profile.credentialReference)
    {
        return false;
    }
    if (availableKeys == nullptr)
    {
        // A locked or temporarily unavailable persistent backend cannot be
        // enumerated. Preserve the non-secret profile metadata so the UI can
        // offer the appropriate unlock/recovery flow.
        return true;
    }
    const ztermy::security::CredentialKey key{
        .profileId = *profile.credentialReference,
        .kind = profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
                    ? ztermy::security::CredentialKind::PrivateKeyPassphrase
                    : ztermy::security::CredentialKind::Password,
    };
    return std::ranges::find(*availableKeys, key) != availableKeys->end();
}

[[nodiscard]] QVariantMap profileVariantMap(const ztermy::ssh::SshProfile &profile, const bool credentialStored)
{
    QVariantMap result{
        {QStringLiteral("id"), utf8QString(profile.id)},
        {QStringLiteral("name"), utf8QString(profile.name)},
        {QStringLiteral("group"), utf8QString(profile.group)},
        {QStringLiteral("host"), utf8QString(profile.host)},
        {QStringLiteral("port"), profile.port},
        {QStringLiteral("username"), utf8QString(profile.username)},
        {QStringLiteral("authentication"), profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
                                               ? QStringLiteral("private-key")
                                               : QStringLiteral("password")},
        {QStringLiteral("privateKeyPath"), utf8QString(profile.privateKeyPath)},
        {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
        {QStringLiteral("credentialStored"), credentialStored},
    };
    if (profile.lastConnectedUtcMs)
    {
        result.insert(QStringLiteral("lastConnectedUtcMs"), *profile.lastConnectedUtcMs);
    }
    return result;
}

[[nodiscard]] ztermy::security::CredentialStorage
defaultCredentialStorage(const ztermy::config::StorageMode mode) noexcept
{
    return mode == ztermy::config::StorageMode::installed ? ztermy::security::CredentialStorage::System
                                                          : ztermy::security::CredentialStorage::Portable;
}

[[nodiscard]] std::optional<ztermy::security::CredentialStorage> parseCredentialStorage(const QString &value)
{
    if (value == QStringLiteral("system"))
    {
        return ztermy::security::CredentialStorage::System;
    }
    if (value == QStringLiteral("portable"))
    {
        return ztermy::security::CredentialStorage::Portable;
    }
    if (value == QStringLiteral("session"))
    {
        return ztermy::security::CredentialStorage::Session;
    }
    return std::nullopt;
}

[[nodiscard]] ztermy::config::CredentialStoragePreference
credentialPreferenceForStorage(const ztermy::security::CredentialStorage storage) noexcept
{
    switch (storage)
    {
        case ztermy::security::CredentialStorage::System:
            return ztermy::config::CredentialStoragePreference::system;
        case ztermy::security::CredentialStorage::Portable:
            return ztermy::config::CredentialStoragePreference::portable;
        case ztermy::security::CredentialStorage::Session:
            return ztermy::config::CredentialStoragePreference::session;
    }
    return ztermy::config::CredentialStoragePreference::automatic;
}

[[nodiscard]] QString credentialVaultErrorMessage(const ztermy::security::CredentialVaultError error)
{
    using enum ztermy::security::CredentialVaultError;
    switch (error)
    {
        case Locked:
            return QCoreApplication::translate("AppController", "Unlock the portable credential vault first.");
        case Unavailable:
            return QCoreApplication::translate("AppController",
                                               "The selected credential store is unavailable in this Windows session.");
        case AccessDenied:
            return QCoreApplication::translate("AppController",
                                               "Windows denied access to the selected credential store.");
        case AuthenticationFailed:
            return QCoreApplication::translate("AppController",
                                               "The vault password is incorrect, or the vault was modified.");
        case WeakMasterPassword:
            return QCoreApplication::translate("AppController", "Use a vault password with at least 8 UTF-8 bytes.");
        case AlreadyInitialized:
            return QCoreApplication::translate("AppController",
                                               "The portable credential vault is already initialized.");
        case CorruptData:
            return QCoreApplication::translate("AppController", "The portable credential vault is damaged or invalid.");
        case UnsupportedVersion:
            return QCoreApplication::translate("AppController",
                                               "This credential vault was created by an unsupported ztermy version.");
        case EmptySecret:
            return QCoreApplication::translate("AppController", "Enter a password or key passphrase before saving it.");
        case SecretTooLarge:
            return QCoreApplication::translate("AppController", "The credential is larger than the supported limit.");
        case NotFound:
            return QCoreApplication::translate("AppController", "No saved credential was found for this host.");
        case InvalidKey:
            return QCoreApplication::translate("AppController", "The credential reference is invalid.");
        case IoError:
            return QCoreApplication::translate("AppController", "The credential store could not be read or written.");
        case CryptoError:
            return QCoreApplication::translate("AppController",
                                               "Windows could not complete the credential encryption operation.");
        case MigrationFailed:
            return QCoreApplication::translate("AppController",
                                               "Credential migration was rolled back because verification failed.");
    }
    return QCoreApplication::translate("AppController", "The credential operation failed.");
}

void logCredentialRollbackResult(std::expected<void, ztermy::security::CredentialVaultError> result,
                                 const char *operation)
{
    if (!result)
    {
        qCWarning(appControllerLog) << "Credential rollback failed during" << operation
                                    << "error=" << static_cast<int>(result.error());
    }
}

[[nodiscard]] ztermy::security::CredentialKind credentialKind(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.authentication == ztermy::ssh::SshAuthenticationMethod::Password
               ? ztermy::security::CredentialKind::Password
               : ztermy::security::CredentialKind::PrivateKeyPassphrase;
}

[[nodiscard]] QString quickConnectError(const ztermy::ssh::SshTargetError error)
{
    using enum ztermy::ssh::SshTargetError;
    switch (error)
    {
        case MissingUsername:
            return QCoreApplication::translate("AppController", "Enter a username before @.");
        case MissingHost:
            return QCoreApplication::translate("AppController", "Enter a host after @.");
        case InvalidPort:
            return QCoreApplication::translate("AppController", "Port must be a number from 1 to 65535.");
        case BracketsRequired:
            return QCoreApplication::translate("AppController",
                                               "Wrap an IPv6 host in brackets, for example user@[::1]:22.");
        case InvalidFormat:
            return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
    }
    return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
}

[[nodiscard]] QString localizedSshFailure(const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshFailureKind;
    if (!failure)
    {
        return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
    }
    switch (*failure)
    {
        case NameResolutionFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH host name resolution failed");
        case ConnectionRefused:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection was refused");
        case TimedOut:
            return QCoreApplication::translate("SshTerminalSession", "SSH operation timed out");
        case TransportError:
            return QCoreApplication::translate("SshTerminalSession", "SSH transport failed");
        case HostKeyChanged:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key changed");
        case HostKeyInvalid:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key could not be verified");
        case AuthenticationRejected:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication was rejected");
        case AuthenticationUnavailable:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication method is unavailable");
        case ChannelOpenFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH terminal channel could not be opened");
        case RemoteClosed:
            return QCoreApplication::translate("SshTerminalSession", "SSH remote host closed the connection");
        case Cancelled:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection cancelled");
        case ProtocolError:
            return QCoreApplication::translate("SshTerminalSession", "SSH protocol error");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

[[nodiscard]] QString localizedSshStatus(const ztermy::ssh::SshConnectionPhase phase,
                                         const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Resolving:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Resolving SSH host");
        case Connecting:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Connecting to SSH host");
        case Handshaking:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Negotiating SSH connection");
        case VerifyingHostKey:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Verifying SSH host key");
        case AwaitingHostKeyConfirmation:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH host key confirmation required");
        case Authenticating:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Authenticating SSH session");
        case OpeningChannel:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Opening SSH terminal");
        case Connected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal connected");
        case Failed:
            return localizedSshFailure(failure);
        case Closing:
        case Disconnected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal disconnected");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

} // namespace

namespace ztermy
{

AppController::AppController(QObject *parent)
    : AppController(applicationDataFile(QStringLiteral("profiles.json")),
                    applicationDataFile(QStringLiteral("known_hosts.json")),
                    applicationDataFile(QStringLiteral("settings.json")), parent)
{
}

AppController::AppController(const QString &profileStorePath, QObject *parent)
    : AppController(profileStorePath, siblingKnownHostsFile(profileStorePath), siblingSettingsFile(profileStorePath),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath),
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, std::move(localSessionFactory),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_profileStore(std::move(profileStorePath)),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          siblingCredentialsFile(m_profileStore.filePath()), security::CredentialStorage::Session)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    loadHostProfiles();
    loadApplicationSettings();
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath), std::move(credentialsPath),
          storageMode,
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_profileStore(std::move(profileStorePath)),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          credentialsPath.isEmpty() ? siblingCredentialsFile(m_profileStore.filePath()) : std::move(credentialsPath),
          defaultCredentialStorage(storageMode))),
      m_defaultCredentialStorage(defaultCredentialStorage(storageMode)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    Q_ASSERT(m_credentialVaults);
    loadHostProfiles();
    loadApplicationSettings();
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
    showActiveTab();
}

void AppController::shutdown() noexcept
{
    clearHostKeyPrompt();
    for (const auto &tab : m_tabs)
    {
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
    }
    m_tabs.clear();
    m_activeTabId.clear();
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
        m_terminal->setSnapshot({});
        m_terminal = nullptr;
    }
}

bool AppController::sshActive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
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
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_profiles.size()));
    for (const ssh::SshProfile &profile : m_profiles)
    {
        result.append(profileVariantMap(profile, profileHasStoredCredential(profile, availableKeys)));
    }
    return result;
}

QVariantList AppController::recentHostProfiles() const
{
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    std::vector<const ssh::SshProfile *> recent;
    recent.reserve(m_profiles.size());
    for (const ssh::SshProfile &profile : m_profiles)
    {
        if (profile.lastConnectedUtcMs)
        {
            recent.push_back(&profile);
        }
    }
    std::ranges::sort(recent, std::greater{}, [](const ssh::SshProfile *profile) {
        return *profile->lastConnectedUtcMs;
    });
    if (recent.size() > 6U)
    {
        recent.resize(6U);
    }

    QVariantList result;
    result.reserve(static_cast<qsizetype>(recent.size()));
    for (const ssh::SshProfile *profile : recent)
    {
        result.append(profileVariantMap(*profile, profileHasStoredCredential(*profile, availableKeys)));
    }
    return result;
}

QVariantList AppController::terminalTabs() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_tabs.size()));
    for (const auto &tab : m_tabs)
    {
        result.append(QVariantMap{
            {QStringLiteral("id"), tab->id},
            {QStringLiteral("title"), tab->title},
            {QStringLiteral("kind"),
             tab->kind == TerminalTabKind::Local ? QStringLiteral("local") : QStringLiteral("ssh")},
            {QStringLiteral("status"), tab->status},
            {QStringLiteral("running"), tab->running},
            {QStringLiteral("connecting"), tab->kind == TerminalTabKind::Ssh
                                               && tab->sshPhase != ssh::SshConnectionPhase::Disconnected
                                               && tab->sshPhase != ssh::SshConnectionPhase::Connected
                                               && tab->sshPhase != ssh::SshConnectionPhase::Closing
                                               && tab->sshPhase != ssh::SshConnectionPhase::Failed},
            {QStringLiteral("failed"), tab->kind == TerminalTabKind::Ssh
                                           && tab->sshPhase == ssh::SshConnectionPhase::Failed
                                           && tab->sshFailure != ssh::SshFailureKind::RemoteClosed},
            {QStringLiteral("remoteClosed"), tab->kind == TerminalTabKind::Ssh
                                                 && tab->sshPhase == ssh::SshConnectionPhase::Failed
                                                 && tab->sshFailure == ssh::SshFailureKind::RemoteClosed},
        });
    }
    return result;
}

QString AppController::activeTerminalTabId() const
{
    return m_activeTabId;
}

QString AppController::terminalSearchQuery() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->searchQuery;
}

int AppController::terminalSearchCurrent() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchCurrent);
}

int AppController::terminalSearchTotal() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchTotal);
}

bool AppController::terminalSearchCaseSensitive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->searchCaseSensitive;
}

QString AppController::themePreference() const
{
    return config::themePreferenceToken(m_settings.theme);
}

qreal AppController::backdropOpacity() const noexcept
{
    return m_settings.backdropOpacity;
}

QString AppController::backdropPreference() const
{
    return config::backdropPreferenceToken(m_settings.backdrop);
}

QString AppController::accentPreference() const
{
    return config::accentPreferenceToken(m_settings.accent);
}

QString AppController::customAccent() const
{
    return m_settings.customAccent;
}

QString AppController::uiFontFamily() const
{
    return m_settings.uiFontFamily;
}

QString AppController::terminalFontFamily() const
{
    return m_settings.terminalFontFamily;
}

int AppController::terminalFontSize() const noexcept
{
    return m_settings.terminalFontSize;
}

bool AppController::showAllTerminalFonts() const noexcept
{
    return m_settings.showAllTerminalFonts;
}

bool AppController::terminalLigatures() const noexcept
{
    return m_settings.terminalLigatures;
}

qreal AppController::terminalBackgroundOpacity() const noexcept
{
    return m_settings.terminalBackgroundOpacity;
}

QString AppController::cursorPreference() const
{
    return config::cursorPreferenceToken(m_settings.cursor);
}

bool AppController::cursorBlink() const noexcept
{
    return m_settings.cursorBlink;
}

bool AppController::copyOnSelect() const noexcept
{
    return m_settings.copyOnSelect;
}

bool AppController::confirmMultilinePaste() const noexcept
{
    return m_settings.confirmMultilinePaste;
}

QString AppController::languagePreference() const
{
    return config::languagePreferenceToken(m_settings.language);
}

void AppController::retranslateUiState()
{
    for (const auto &tab : m_tabs)
    {
        if (tab->kind == TerminalTabKind::Local)
        {
            tab->status = tab->running ? QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local PowerShell connected")
                                       : QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local terminal stopped");
        }
        else
        {
            tab->status = localizedSshStatus(tab->sshPhase, tab->sshFailure);
        }
    }
    setCredentialOperationError({});
    showActiveTab();
    emit terminalTabsChanged();
}

QString AppController::credentialStoragePreference() const
{
    return config::credentialStoragePreferenceToken(m_settings.credentialStorage);
}

QString AppController::effectiveCredentialStorage() const
{
    return QString::fromLatin1(security::credentialStorageToken(m_credentialVaults->storage()));
}

bool AppController::portableVaultInitialized() const noexcept
{
    return m_credentialVaults->portableInitialized();
}

bool AppController::portableVaultLocked() const noexcept
{
    return m_credentialVaults->portableLocked();
}

QString AppController::credentialOperationError() const
{
    return m_credentialOperationError;
}

QString AppController::startLocalTerminal()
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return {};
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->title = tr("PowerShell %1").arg(m_nextLocalTabNumber++);
    tab->status = tr("Starting local terminal...");
    tab->kind = TerminalTabKind::Local;
    tab->local = m_localSessionFactory();
    if (!tab->local)
    {
        return {};
    }
    QString tabId = tab->id;
    connectLocalTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->local)
    {
        return {};
    }
    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
    if (error)
    {
        created->status = tr("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        showActiveTab();
        emit terminalTabsChanged();
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return tabId;
}

bool AppController::activateTerminalTab(const QString &id)
{
    if (findTab(id) == nullptr)
    {
        return false;
    }
    if (m_activeTabId == id)
    {
        showActiveTab();
        return true;
    }
    m_activeTabId = id;
    emit activeTerminalTabChanged();
    emit sshActiveChanged();
    emit terminalSearchChanged();
    showActiveTab();
    return true;
}

bool AppController::closeTerminalTab(const QString &id)
{
    const auto position = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &tab) {
        return tab->id;
    });
    if (position == m_tabs.end())
    {
        return false;
    }

    const bool closingActive = (*position)->id == m_activeTabId;
    const auto index = static_cast<std::size_t>(std::distance(m_tabs.begin(), position));
    if ((*position)->id == m_hostKeyTabId)
    {
        clearHostKeyPrompt();
    }
    if ((*position)->local)
    {
        (*position)->local->stop();
    }
    if ((*position)->ssh)
    {
        (*position)->ssh->stop();
    }
    m_tabs.erase(position);
    emit terminalTabsChanged();

    if (closingActive)
    {
        if (m_tabs.empty())
        {
            m_activeTabId.clear();
            emit activeTerminalTabChanged();
            emit sshActiveChanged();
            emit terminalSearchChanged();
            showActiveTab();
        }
        else
        {
            const std::size_t nextIndex = std::min(index, m_tabs.size() - 1U);
            m_activeTabId.clear();
            activateTerminalTab(m_tabs[nextIndex]->id);
        }
    }
    return true;
}

void AppController::searchTerminal(const QString &query, const bool backwards, const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (query.isEmpty())
    {
        clearTerminalSearch();
        return;
    }

    tab->searchQuery = query;
    tab->searchCaseSensitive = caseSensitive;
    if (tab->ssh)
    {
        tab->ssh->search(query, backwards, caseSensitive);
    }
    else if (tab->local)
    {
        tab->local->search(query, backwards, caseSensitive);
    }
    emit terminalSearchChanged();
}

void AppController::clearTerminalSearch()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->searchQuery.clear();
    tab->searchCurrent = 0;
    tab->searchTotal = 0;
    if (tab->ssh)
    {
        tab->ssh->clearSearch();
    }
    else if (tab->local)
    {
        tab->local->clearSearch();
    }
    emit terminalSearchChanged();
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath, const QString &passphrase)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || privateKeyPath.trimmed().isEmpty() || port <= 0
        || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and private-key fields"));
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
        .knownHostsPath = m_knownHostsPath,
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
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and password fields"));
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
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::startSshConnection(ssh::SshConnectionRequest request, QString sourceProfileId)
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return false;
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->title = QStringLiteral("%1@%2").arg(request.username, request.host);
    tab->status = tr("Starting SSH connection...");
    tab->kind = TerminalTabKind::Ssh;
    tab->sourceProfileId = std::move(sourceProfileId);
    tab->sshPhase = ssh::SshConnectionPhase::Resolving;
    tab->ssh = std::make_unique<ssh::SshTerminalSession>();
    const QString tabId = tab->id;
    connectSshTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->ssh)
    {
        return false;
    }
    const std::error_code error = created->ssh->start(std::move(request), {.columns = 100, .rows = 30});
    if (error)
    {
        closeTerminalTab(tabId);
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
                                    const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                    const QString &group)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, {}, false, false);
}

bool AppController::saveHostProfileWithCredential(const QString &id, const QString &name, const QString &host,
                                                  const int port, const QString &username,
                                                  const QString &authentication, const QString &privateKeyPath,
                                                  const bool privateKeyPassphraseRequired, const QString &group,
                                                  const QString &secret, const bool rememberCredential)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, secret, rememberCredential, true);
}

bool AppController::saveAndConnectHostProfile(const QString &id, const QString &name, const QString &host,
                                              const int port, const QString &username, const QString &authentication,
                                              const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                              const QString &group, const QString &secret,
                                              const bool rememberCredential)
{
    const QString profileId =
        id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    if (!saveHostProfileInternal(profileId, name, host, port, username, authentication, privateKeyPath,
                                 privateKeyPassphraseRequired, group, secret, rememberCredential, true))
    {
        return false;
    }
    if (connectHostProfile(profileId, rememberCredential ? QString{} : secret))
    {
        return true;
    }
    if (m_credentialOperationError.isEmpty())
    {
        setCredentialOperationError(tr("The profile was saved, but the connection could not be started."));
    }
    return false;
}

bool AppController::saveHostProfileInternal(const QString &id, const QString &name, const QString &host, const int port,
                                            const QString &username, const QString &authentication,
                                            const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                            const QString &group, const QString &secret, const bool rememberCredential,
                                            const bool manageCredential)
{
    const QString normalizedHost = host.trimmed();
    const QString normalizedName = name.trimmed().isEmpty() ? normalizedHost : name.trimmed();
    const QString normalizedGroup = group.trimmed();
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

    ssh::SshProfile profile{
        .id = utf8String(profileId),
        .name = utf8String(normalizedName),
        .group = utf8String(normalizedGroup),
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
    std::optional<security::CredentialKey> previousKey;
    if (existing == updated.end())
    {
        updated.push_back(profile);
    }
    else
    {
        profile.lastConnectedUtcMs = existing->lastConnectedUtcMs;
        if (existing->credentialReference)
        {
            previousKey =
                security::CredentialKey{.profileId = *existing->credentialReference, .kind = credentialKind(*existing)};
        }
        if (!manageCredential
            || (rememberCredential && secret.isEmpty() && existing->authentication == profile.authentication))
        {
            profile.credentialReference = existing->credentialReference;
        }
        *existing = profile;
    }

    const security::CredentialKey desiredKey{.profileId = profile.id, .kind = credentialKind(profile)};
    const bool shouldStore = manageCredential && rememberCredential && !secret.isEmpty();
    if (shouldStore && m_credentialVaults->storage() == security::CredentialStorage::Portable
        && !m_credentialVaults->portableInitialized())
    {
        setCredentialOperationError(
            tr("Create the portable credential vault in Settings > Security before saving a secret."));
        return false;
    }
    const bool shouldRemovePrevious =
        manageCredential && previousKey
        && (!rememberCredential || (shouldStore && *previousKey != desiredKey)
            || (rememberCredential && secret.isEmpty() && profile.credentialReference != previousKey->profileId));
    std::optional<security::SensitiveByteArray> previousDesiredSecret;
    std::optional<security::SensitiveByteArray> removedPreviousSecret;

    if (shouldStore)
    {
        auto previous = m_credentialVaults->active().read(desiredKey);
        if (previous)
        {
            previousDesiredSecret = std::move(*previous);
        }
        else if (previous.error() != security::CredentialVaultError::NotFound)
        {
            setCredentialOperationError(credentialVaultErrorMessage(previous.error()));
            return false;
        }
        auto stored = m_credentialVaults->active().store(desiredKey, security::SensitiveByteArray(secret.toUtf8()));
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return false;
        }
        profile.credentialReference = profile.id;
        const auto target = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
        Q_ASSERT(target != updated.end());
        target->credentialReference = profile.credentialReference;
    }

    const auto rollbackDesiredCredential = [&] {
        if (!shouldStore)
        {
            return;
        }
        if (previousDesiredSecret)
        {
            const std::string_view bytes = previousDesiredSecret->view();
            logCredentialRollbackResult(m_credentialVaults->active().store(
                                            desiredKey, security::SensitiveByteArray(QByteArray(
                                                            bytes.data(), static_cast<qsizetype>(bytes.size())))),
                                        "profile-save restore");
        }
        else
        {
            logCredentialRollbackResult(m_credentialVaults->active().remove(desiredKey), "profile-save removal");
        }
    };

    if (shouldRemovePrevious && previousKey)
    {
        auto previous = m_credentialVaults->active().read(*previousKey);
        if (previous)
        {
            removedPreviousSecret = std::move(*previous);
        }
        else if (previous.error() != security::CredentialVaultError::NotFound)
        {
            rollbackDesiredCredential();
            setCredentialOperationError(credentialVaultErrorMessage(previous.error()));
            return false;
        }
        const auto removed = m_credentialVaults->active().remove(*previousKey);
        if (!removed && removed.error() != security::CredentialVaultError::NotFound)
        {
            rollbackDesiredCredential();
            setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
            return false;
        }
    }

    if (!m_profileStore.save(updated))
    {
        if (shouldStore)
        {
            rollbackDesiredCredential();
        }
        if (shouldRemovePrevious && previousKey && removedPreviousSecret)
        {
            const std::string_view bytes = removedPreviousSecret->view();
            logCredentialRollbackResult(m_credentialVaults->active().store(
                                            *previousKey, security::SensitiveByteArray(QByteArray(
                                                              bytes.data(), static_cast<qsizetype>(bytes.size())))),
                                        "previous credential restore");
        }
        qCWarning(appControllerLog) << "Unable to persist SSH profiles";
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::duplicateHostProfile(const QString &id)
{
    const auto source = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (source == m_profiles.end())
    {
        return false;
    }

    const QString baseName = utf8QString(source->name) + QStringLiteral(" copy");
    QString copyName = baseName;
    for (int suffix = 2;
         std::ranges::any_of(m_profiles,
                             [&copyName](const ssh::SshProfile &profile) {
                                 return utf8QString(profile.name).compare(copyName, Qt::CaseInsensitive) == 0;
                             });
         ++suffix)
    {
        copyName = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    }

    ssh::SshProfile copy = *source;
    copy.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    copy.name = utf8String(copyName);
    copy.credentialReference.reset();
    copy.lastConnectedUtcMs.reset();
    std::vector<ssh::SshProfile> updated = m_profiles;
    updated.push_back(std::move(copy));
    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist duplicated SSH profile";
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
    std::optional<security::CredentialKey> key;
    std::optional<security::SensitiveByteArray> removedSecret;
    if (profile->credentialReference)
    {
        key = security::CredentialKey{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
        auto current = m_credentialVaults->active().read(*key);
        if (current)
        {
            removedSecret = std::move(*current);
        }
        else if (current.error() != security::CredentialVaultError::NotFound)
        {
            setCredentialOperationError(credentialVaultErrorMessage(current.error()));
            return false;
        }
        const auto removed = m_credentialVaults->active().remove(*key);
        if (!removed && removed.error() != security::CredentialVaultError::NotFound)
        {
            setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
            return false;
        }
    }
    updated.erase(profile);

    if (!m_profileStore.save(updated))
    {
        if (key && removedSecret)
        {
            const std::string_view bytes = removedSecret->view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    *key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "deleted profile restore");
        }
        qCWarning(appControllerLog) << "Unable to persist SSH profiles after deletion";
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::forgetHostCredential(const QString &id)
{
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == updated.end() || !profile->credentialReference)
    {
        return false;
    }
    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto current = m_credentialVaults->active().read(key);
    std::optional<security::SensitiveByteArray> removedSecret;
    if (current)
    {
        removedSecret = std::move(*current);
    }
    else if (current.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(current.error()));
        return false;
    }
    const auto removed = m_credentialVaults->active().remove(key);
    if (!removed && removed.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    profile->credentialReference.reset();
    if (!m_profileStore.save(updated))
    {
        if (removedSecret)
        {
            const std::string_view bytes = removedSecret->view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "forgotten credential restore");
        }
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::saveHostCredential(const QString &id, const QString &secret)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || secret.isEmpty())
    {
        return false;
    }
    return saveHostProfileWithCredential(
        id, utf8QString(profile->name), utf8QString(profile->host), profile->port, utf8QString(profile->username),
        profile->authentication == ssh::SshAuthenticationMethod::PrivateKey ? QStringLiteral("private-key")
                                                                            : QStringLiteral("password"),
        utf8QString(profile->privateKeyPath), profile->privateKeyPassphraseRequired, utf8QString(profile->group),
        secret, true);
}

QString AppController::readHostCredential(const QString &id)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !profile->credentialReference)
    {
        setCredentialOperationError(tr("This host profile has no saved credential."));
        return {};
    }

    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto secret = m_credentialVaults->active().read(key);
    if (!secret)
    {
        setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
        return {};
    }

    const std::string_view bytes = secret->view();
    QString result = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    setCredentialOperationError({});
    return result;
}

bool AppController::connectHostProfile(const QString &id, const QString &secret)
{
    const std::string profileId = utf8String(id);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    security::SensitiveByteArray connectionSecret(secret.toUtf8());
    if (connectionSecret.empty() && profile->credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile->credentialReference, .kind = credentialKind(*profile)});
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return false;
        }
        connectionSecret = std::move(*stored);
    }
    if ((profile->authentication == ssh::SshAuthenticationMethod::Password || profile->privateKeyPassphraseRequired)
        && connectionSecret.empty())
    {
        setCredentialOperationError(tr("Enter the credential required by this host."));
        return false;
    }
    ssh::SshConnectionRequest request{
        .host = utf8QString(profile->host),
        .port = profile->port,
        .username = utf8QString(profile->username),
        .authentication = profile->authentication,
        .privateKeyPath = utf8QString(profile->privateKeyPath),
        .secret = std::move(connectionSecret),
        .knownHostsPath = m_knownHostsPath,
    };
    setCredentialOperationError({});
    return startSshConnection(std::move(request), id);
}

QVariantMap AppController::parseQuickConnectTarget(const QString &target) const
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    if (!parsed)
    {
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("error"), quickConnectError(parsed.error())},
        };
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("error"), QString{}},
        {QStringLiteral("username"), utf8QString(parsed->username)},
        {QStringLiteral("host"), utf8QString(parsed->host)},
        {QStringLiteral("port"), parsed->port},
    };
}

bool AppController::connectQuick(const QString &target, const QString &authentication, const QString &privateKeyPath,
                                 const bool privateKeyPassphraseRequired, const QString &secret,
                                 const bool shouldSaveProfile, const QString &profileName, const QString &group)
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    const auto authenticationMethod =
        authentication == QStringLiteral("private-key") ? std::optional{ssh::SshAuthenticationMethod::PrivateKey}
        : authentication == QStringLiteral("password")  ? std::optional{ssh::SshAuthenticationMethod::Password}
                                                        : std::nullopt;
    if (!parsed || !authenticationMethod)
    {
        return false;
    }
    const QString host = utf8QString(parsed->host);
    const QString username = utf8QString(parsed->username);
    const QString normalizedKeyPath = privateKeyPath.trimmed();
    if ((*authenticationMethod == ssh::SshAuthenticationMethod::Password && secret.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && normalizedKeyPath.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired
            && secret.isEmpty()))
    {
        return false;
    }

    if (shouldSaveProfile)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!saveHostProfileWithCredential(id, profileName, host, parsed->port, username, authentication,
                                           normalizedKeyPath, privateKeyPassphraseRequired, group, secret, true))
        {
            return false;
        }
        return connectHostProfile(id, {});
    }

    ssh::SshConnectionRequest request{
        .host = host,
        .port = parsed->port,
        .username = username,
        .authentication = *authenticationMethod,
        .privateKeyPath =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey ? normalizedKeyPath : QString{},
        .secret = security::SensitiveByteArray(secret.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::saveApplicationSettings(const QString &theme, const qreal backdropOpacity, const QString &backdrop,
                                            const QString &accent, const QString &customAccent,
                                            const QString &uiFontFamily, const QString &fontFamily, const int fontSize,
                                            const bool showAllFonts, const bool ligatures,
                                            const qreal terminalBackgroundOpacity, const QString &cursor,
                                            const bool cursorShouldBlink, const bool shouldCopyOnSelect,
                                            const bool shouldConfirmMultilinePaste, const QString &language)
{
    const auto parsedTheme = config::parseThemePreference(theme);
    const auto parsedBackdrop = config::parseBackdropPreference(backdrop);
    const auto parsedAccent = config::parseAccentPreference(accent);
    const auto parsedCursor = config::parseCursorPreference(cursor);
    const auto parsedLanguage = config::parseLanguagePreference(language);
    if (!parsedTheme || !parsedBackdrop || !parsedAccent || !parsedCursor || !parsedLanguage)
    {
        return false;
    }

    return persistApplicationSettings({
        .theme = *parsedTheme,
        .backdropOpacity = backdropOpacity,
        .backdrop = *parsedBackdrop,
        .accent = *parsedAccent,
        .customAccent = customAccent.trimmed().toUpper(),
        .uiFontFamily = uiFontFamily,
        .terminalFontFamily = fontFamily,
        .terminalFontSize = fontSize,
        .showAllTerminalFonts = showAllFonts,
        .terminalLigatures = ligatures,
        .terminalBackgroundOpacity = terminalBackgroundOpacity,
        .cursor = *parsedCursor,
        .cursorBlink = cursorShouldBlink,
        .copyOnSelect = shouldCopyOnSelect,
        .confirmMultilinePaste = shouldConfirmMultilinePaste,
        .credentialStorage = m_settings.credentialStorage,
        .language = *parsedLanguage,
    });
}

bool AppController::resetApplicationSettings()
{
    config::ApplicationSettings defaults;
    defaults.credentialStorage = m_settings.credentialStorage;
    return persistApplicationSettings(defaults);
}

bool AppController::initializePortableCredentialVault(const QString &masterPassword)
{
    auto initialized = m_credentialVaults->initializePortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!initialized)
    {
        setCredentialOperationError(credentialVaultErrorMessage(initialized.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    return true;
}

bool AppController::unlockPortableCredentialVault(const QString &masterPassword)
{
    auto unlocked = m_credentialVaults->unlockPortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!unlocked)
    {
        setCredentialOperationError(credentialVaultErrorMessage(unlocked.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    return true;
}

bool AppController::changePortableVaultMasterPassword(const QString &masterPassword)
{
    auto changed =
        m_credentialVaults->changePortableMasterPassword(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!changed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(changed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

void AppController::lockPortableCredentialVault()
{
    m_credentialVaults->lockPortable();
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
}

bool AppController::migrateCredentialStorage(const QString &target, const bool removeSource)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    const security::CredentialStorage previousStorage = m_credentialVaults->storage();
    if (*parsedTarget == previousStorage)
    {
        setCredentialOperationError({});
        return true;
    }
    auto migrated = m_credentialVaults->migrate(*parsedTarget, false);
    if (!migrated)
    {
        setCredentialOperationError(credentialVaultErrorMessage(migrated.error()));
        return false;
    }

    config::ApplicationSettings updatedSettings = m_settings;
    updatedSettings.credentialStorage = credentialPreferenceForStorage(*parsedTarget);
    if (!persistApplicationSettings(updatedSettings))
    {
        m_credentialVaults->select(previousStorage);
        setCredentialOperationError(tr(
            "Credentials were copied, but the storage preference could not be saved. The old store remains active."));
        emit credentialVaultChanged();
        return false;
    }
    emit hostProfilesChanged();
    if (removeSource)
    {
        auto cleaned = m_credentialVaults->removeAll(previousStorage);
        if (!cleaned)
        {
            setCredentialOperationError(tr("Migration succeeded, but credentials remain in the previous store."));
            emit credentialVaultChanged();
            return false;
        }
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::removeAllSavedCredentials()
{
    std::vector<security::CredentialKey> keys;
    const bool emptyUninitializedPortable = m_credentialVaults->storage() == security::CredentialStorage::Portable
                                            && !m_credentialVaults->portableInitialized();
    if (!emptyUninitializedPortable)
    {
        auto listedKeys = m_credentialVaults->active().listKeys();
        if (!listedKeys)
        {
            setCredentialOperationError(credentialVaultErrorMessage(listedKeys.error()));
            return false;
        }
        keys = std::move(*listedKeys);
    }
    std::vector<std::pair<security::CredentialKey, security::SensitiveByteArray>> backup;
    backup.reserve(keys.size());
    for (const security::CredentialKey &key : keys)
    {
        auto secret = m_credentialVaults->active().read(key);
        if (!secret)
        {
            setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
            return false;
        }
        backup.emplace_back(key, std::move(*secret));
    }
    auto removed = m_credentialVaults->removeAll();
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    std::vector<ssh::SshProfile> updated = m_profiles;
    for (ssh::SshProfile &profile : updated)
    {
        profile.credentialReference.reset();
    }
    if (!m_profileStore.save(updated))
    {
        for (const auto &[key, secret] : backup)
        {
            const std::string_view bytes = secret.view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "remove-all restore");
        }
        setCredentialOperationError(tr("Credentials were restored because host profiles could not be updated."));
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    return true;
}

bool AppController::clearCredentialStorage(const QString &target)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    if (*parsedTarget == m_credentialVaults->storage())
    {
        return removeAllSavedCredentials();
    }

    auto removed = m_credentialVaults->removeAll(*parsedTarget);
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    clearHostKeyPrompt();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->confirmHostKey(remember);
    }
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    clearHostKeyPrompt();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->rejectHostKey();
    }
}

void AppController::connectLocalTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::runningChanged, this,
                     [this, tabId](const bool running) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->running = running;
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_activeTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
}

void AppController::connectSshTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::runningChanged, this, [this, tabId](const bool running) {
        if (TerminalTab *updated = findTab(tabId))
        {
            updated->running = running;
            emit terminalTabsChanged();
        }
    });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::phaseChanged, this,
                     [this, tabId](const ssh::SshConnectionPhase phase) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshPhase = phase;
                             if (phase == ssh::SshConnectionPhase::Connected)
                             {
                                 recordRecentConnection(*updated);
                             }
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::failureOccurred, this,
                     [this, tabId](const ssh::SshFailureKind failure) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshFailure = failure;
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_activeTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(algorithm, fingerprint, false);
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this, tabId](const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(tr("SSH host key changed; connection blocked"));
                         }
                     });
}

void AppController::recordRecentConnection(TerminalTab &tab)
{
    if (tab.recentConnectionRecorded || tab.sourceProfileId.isEmpty())
    {
        return;
    }
    tab.recentConnectionRecorded = true;
    const auto profile = std::ranges::find(m_profiles, utf8String(tab.sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return;
    }

    const std::optional<std::int64_t> previous = profile->lastConnectedUtcMs;
    profile->lastConnectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (!m_profileStore.save(m_profiles))
    {
        profile->lastConnectedUtcMs = previous;
        qCWarning(appControllerLog) << "Unable to persist recent SSH connection metadata";
        return;
    }
    emit hostProfilesChanged();
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
    QObject::connect(m_terminal, &ui::TerminalItem::scrollRequested, this, &AppController::requestScroll);
    QObject::connect(m_terminal, &ui::TerminalItem::selectionRequested, this, &AppController::requestSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::clearSelectionRequested, this, &AppController::clearSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::copyRequested, this, &AppController::copySelection);
}

void AppController::queueInput(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->queueInput(bytes);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->queueInput(bytes);
    }
}

void AppController::queuePaste(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->queuePaste(bytes);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->queuePaste(bytes);
    }
}

void AppController::requestScroll(const int rows)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestScroll(rows);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestScroll(rows);
    }
}

void AppController::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                     const quint16 endRow, const bool rectangular)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
}

void AppController::clearSelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->clearSelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->clearSelection();
    }
}

void AppController::copySelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->copySelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->copySelection();
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

AppController::TerminalTab *AppController::activeTab()
{
    return findTab(m_activeTabId);
}

const AppController::TerminalTab *AppController::activeTab() const
{
    return findTab(m_activeTabId);
}

AppController::TerminalTab *AppController::findTab(const QString &id)
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTab(const QString &id) const
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

void AppController::showActiveTab()
{
    if (m_terminal == nullptr)
    {
        return;
    }
    const TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(tr("No terminal session"));
        return;
    }
    m_terminal->setSnapshot(tab->snapshot);
    m_terminal->setStatusText(tab->status);
    m_terminal->requestCurrentSize();
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
    if (!m_hostKeyPromptVisible && m_hostKeyTabId.isEmpty() && m_hostKeyAlgorithm.isEmpty()
        && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyTabId.clear();
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

void AppController::loadApplicationSettings()
{
    auto settings = m_settingsStore.load();
    if (!settings)
    {
        qCWarning(appControllerLog) << "Unable to load application settings; using defaults";
        return;
    }
    m_settings = std::move(*settings);
    security::CredentialStorage selected = m_defaultCredentialStorage;
    switch (m_settings.credentialStorage)
    {
        case config::CredentialStoragePreference::system:
            selected = security::CredentialStorage::System;
            break;
        case config::CredentialStoragePreference::portable:
            selected = security::CredentialStorage::Portable;
            break;
        case config::CredentialStoragePreference::session:
            selected = security::CredentialStorage::Session;
            break;
        case config::CredentialStoragePreference::automatic:
            break;
    }
    m_credentialVaults->select(selected);
}

bool AppController::persistApplicationSettings(const config::ApplicationSettings &settings)
{
    if (!m_settingsStore.save(settings))
    {
        qCWarning(appControllerLog) << "Unable to persist application settings";
        return false;
    }
    if (m_settings == settings)
    {
        return true;
    }
    m_settings = settings;
    emit applicationSettingsChanged();
    return true;
}

void AppController::setCredentialOperationError(QString message)
{
    if (m_credentialOperationError == message)
    {
        return;
    }
    m_credentialOperationError = std::move(message);
    emit credentialVaultChanged();
}

} // namespace ztermy
