#pragma once

#include "application/security/CredentialVaultCoordinator.h"
#include "application/ssh/SshTerminalSession.h"
#include "application/terminal/LocalTerminalSession.h"
#include "core/config/ApplicationPaths.h"
#include "core/config/ApplicationSettings.h"
#include "infrastructure/ssh/SshProfileStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ztermy::ui
{
class TerminalItem;
}

namespace ztermy
{

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sshActive READ sshActive NOTIFY sshActiveChanged)
    Q_PROPERTY(bool hostKeyPromptVisible READ hostKeyPromptVisible NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyAlgorithm READ hostKeyAlgorithm NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyFingerprint READ hostKeyFingerprint NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(bool hostKeyChangedWarning READ hostKeyChangedWarning NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString defaultPrivateKeyPath READ defaultPrivateKeyPath CONSTANT)
    Q_PROPERTY(QVariantList hostProfiles READ hostProfiles NOTIFY hostProfilesChanged)
    Q_PROPERTY(QVariantList recentHostProfiles READ recentHostProfiles NOTIFY hostProfilesChanged)
    Q_PROPERTY(QVariantList terminalTabs READ terminalTabs NOTIFY terminalTabsChanged)
    Q_PROPERTY(QString activeTerminalTabId READ activeTerminalTabId NOTIFY activeTerminalTabChanged)
    Q_PROPERTY(QString terminalSearchQuery READ terminalSearchQuery NOTIFY terminalSearchChanged)
    Q_PROPERTY(int terminalSearchCurrent READ terminalSearchCurrent NOTIFY terminalSearchChanged)
    Q_PROPERTY(int terminalSearchTotal READ terminalSearchTotal NOTIFY terminalSearchChanged)
    Q_PROPERTY(bool terminalSearchCaseSensitive READ terminalSearchCaseSensitive NOTIFY terminalSearchChanged)
    Q_PROPERTY(QString themePreference READ themePreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(qreal backdropOpacity READ backdropOpacity NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString backdropPreference READ backdropPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString accentPreference READ accentPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString customAccent READ customAccent NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString terminalFontFamily READ terminalFontFamily NOTIFY applicationSettingsChanged)
    Q_PROPERTY(int terminalFontSize READ terminalFontSize NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool showAllTerminalFonts READ showAllTerminalFonts NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool terminalLigatures READ terminalLigatures NOTIFY applicationSettingsChanged)
    Q_PROPERTY(qreal terminalBackgroundOpacity READ terminalBackgroundOpacity NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString cursorPreference READ cursorPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool confirmMultilinePaste READ confirmMultilinePaste NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString languagePreference READ languagePreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString credentialStoragePreference READ credentialStoragePreference NOTIFY credentialVaultChanged)
    Q_PROPERTY(QString effectiveCredentialStorage READ effectiveCredentialStorage NOTIFY credentialVaultChanged)
    Q_PROPERTY(bool portableVaultInitialized READ portableVaultInitialized NOTIFY credentialVaultChanged)
    Q_PROPERTY(bool portableVaultLocked READ portableVaultLocked NOTIFY credentialVaultChanged)
    Q_PROPERTY(QString credentialOperationError READ credentialOperationError NOTIFY credentialVaultChanged)

public:
    using LocalTerminalSessionFactory = std::function<std::unique_ptr<terminal::LocalTerminalSessionBackend>()>;

    explicit AppController(QObject *parent = nullptr);
    explicit AppController(const QString &profileStorePath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, LocalTerminalSessionFactory localSessionFactory,
                  QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                  LocalTerminalSessionFactory localSessionFactory, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QString credentialsPath,
                  config::StorageMode storageMode, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QString credentialsPath,
                  config::StorageMode storageMode, LocalTerminalSessionFactory localSessionFactory,
                  QObject *parent = nullptr);
    ~AppController() override;

    AppController(const AppController &) = delete;
    AppController &operator=(const AppController &) = delete;

    void attachTerminal(ui::TerminalItem *terminal);
    void shutdown() noexcept;

    [[nodiscard]] bool sshActive() const noexcept;
    [[nodiscard]] bool hostKeyPromptVisible() const noexcept;
    [[nodiscard]] QString hostKeyAlgorithm() const;
    [[nodiscard]] QString hostKeyFingerprint() const;
    [[nodiscard]] bool hostKeyChangedWarning() const noexcept;
    [[nodiscard]] QString defaultPrivateKeyPath() const;
    [[nodiscard]] QVariantList hostProfiles() const;
    [[nodiscard]] QVariantList recentHostProfiles() const;
    [[nodiscard]] QVariantList terminalTabs() const;
    [[nodiscard]] QString activeTerminalTabId() const;
    [[nodiscard]] QString terminalSearchQuery() const;
    [[nodiscard]] int terminalSearchCurrent() const noexcept;
    [[nodiscard]] int terminalSearchTotal() const noexcept;
    [[nodiscard]] bool terminalSearchCaseSensitive() const noexcept;
    [[nodiscard]] QString themePreference() const;
    [[nodiscard]] qreal backdropOpacity() const noexcept;
    [[nodiscard]] QString backdropPreference() const;
    [[nodiscard]] QString accentPreference() const;
    [[nodiscard]] QString customAccent() const;
    [[nodiscard]] QString uiFontFamily() const;
    [[nodiscard]] QString terminalFontFamily() const;
    [[nodiscard]] int terminalFontSize() const noexcept;
    [[nodiscard]] bool showAllTerminalFonts() const noexcept;
    [[nodiscard]] bool terminalLigatures() const noexcept;
    [[nodiscard]] qreal terminalBackgroundOpacity() const noexcept;
    [[nodiscard]] QString cursorPreference() const;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] bool copyOnSelect() const noexcept;
    [[nodiscard]] bool confirmMultilinePaste() const noexcept;
    [[nodiscard]] QString languagePreference() const;
    void retranslateUiState();
    [[nodiscard]] QString credentialStoragePreference() const;
    [[nodiscard]] QString effectiveCredentialStorage() const;
    [[nodiscard]] bool portableVaultInitialized() const noexcept;
    [[nodiscard]] bool portableVaultLocked() const noexcept;
    [[nodiscard]] QString credentialOperationError() const;

    Q_INVOKABLE QString startLocalTerminal();
    Q_INVOKABLE bool activateTerminalTab(const QString &id);
    Q_INVOKABLE bool closeTerminalTab(const QString &id);
    Q_INVOKABLE void searchTerminal(const QString &query, bool backwards, bool caseSensitive);
    Q_INVOKABLE void clearTerminalSearch();
    Q_INVOKABLE bool connectPrivateKey(const QString &host, int port, const QString &username,
                                       const QString &privateKeyPath, const QString &passphrase);
    Q_INVOKABLE bool connectPassword(const QString &host, int port, const QString &username, const QString &password);
    Q_INVOKABLE bool saveHostProfile(const QString &id, const QString &name, const QString &host, int port,
                                     const QString &username, const QString &authentication,
                                     const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                     const QString &group);
    Q_INVOKABLE bool saveHostProfileWithCredential(const QString &id, const QString &name, const QString &host,
                                                   int port, const QString &username, const QString &authentication,
                                                   const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                                   const QString &group, const QString &secret,
                                                   bool rememberCredential);
    Q_INVOKABLE bool saveAndConnectHostProfile(const QString &id, const QString &name, const QString &host, int port,
                                               const QString &username, const QString &authentication,
                                               const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                               const QString &group, const QString &secret, bool rememberCredential);
    Q_INVOKABLE bool duplicateHostProfile(const QString &id);
    Q_INVOKABLE bool deleteHostProfile(const QString &id);
    Q_INVOKABLE bool forgetHostCredential(const QString &id);
    Q_INVOKABLE bool saveHostCredential(const QString &id, const QString &secret);
    [[nodiscard]] Q_INVOKABLE QString readHostCredential(const QString &id);
    Q_INVOKABLE bool connectHostProfile(const QString &id, const QString &secret);
    [[nodiscard]] Q_INVOKABLE QVariantMap parseQuickConnectTarget(const QString &target) const;
    Q_INVOKABLE bool connectQuick(const QString &target, const QString &authentication, const QString &privateKeyPath,
                                  bool privateKeyPassphraseRequired, const QString &secret, bool saveProfile,
                                  const QString &profileName, const QString &group);
    Q_INVOKABLE bool saveApplicationSettings(const QString &theme, qreal backdropOpacity, const QString &backdrop,
                                             const QString &accent, const QString &customAccent,
                                             const QString &uiFontFamily, const QString &fontFamily, int fontSize,
                                             bool showAllFonts, bool ligatures, qreal terminalBackgroundOpacity,
                                             const QString &cursor, bool cursorShouldBlink, bool shouldCopyOnSelect,
                                             bool shouldConfirmMultilinePaste, const QString &language);
    Q_INVOKABLE bool resetApplicationSettings();
    Q_INVOKABLE bool initializePortableCredentialVault(const QString &masterPassword);
    Q_INVOKABLE bool unlockPortableCredentialVault(const QString &masterPassword);
    Q_INVOKABLE bool changePortableVaultMasterPassword(const QString &masterPassword);
    Q_INVOKABLE void lockPortableCredentialVault();
    Q_INVOKABLE bool migrateCredentialStorage(const QString &target, bool removeSource);
    Q_INVOKABLE bool removeAllSavedCredentials();
    Q_INVOKABLE bool clearCredentialStorage(const QString &target);
    Q_INVOKABLE void acceptHostKey(bool remember);
    Q_INVOKABLE void rejectHostKey();

signals:
    void sshActiveChanged();
    void hostKeyPromptChanged();
    void hostProfilesChanged();
    void terminalTabsChanged();
    void activeTerminalTabChanged();
    void terminalSearchChanged();
    void applicationSettingsChanged();
    void credentialVaultChanged();

private:
    enum class TerminalTabKind : std::uint8_t
    {
        Local,
        Ssh,
    };

    struct TerminalTab final
    {
        QString id;
        QString title;
        QString status;
        TerminalTabKind kind = TerminalTabKind::Local;
        ssh::SshConnectionPhase sshPhase = ssh::SshConnectionPhase::Disconnected;
        std::optional<ssh::SshFailureKind> sshFailure;
        terminal::TerminalSnapshotPtr snapshot;
        std::unique_ptr<terminal::LocalTerminalSessionBackend> local;
        std::unique_ptr<ssh::SshTerminalSession> ssh;
        QString searchQuery;
        QString sourceProfileId;
        std::uint32_t searchCurrent = 0;
        std::uint32_t searchTotal = 0;
        bool searchCaseSensitive = false;
        bool running = false;
        bool recentConnectionRecorded = false;
    };

    void connectTerminalSignals();
    void connectLocalTabSignals(TerminalTab &tab);
    void connectSshTabSignals(TerminalTab &tab);
    void queueInput(const QByteArray &bytes);
    void queuePaste(const QByteArray &bytes);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void requestScroll(int rows);
    void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelection();
    void copySelection();
    void setHostKeyPrompt(QString algorithm, QString fingerprint, bool changed);
    void clearHostKeyPrompt();
    void loadHostProfiles();
    void loadApplicationSettings();
    [[nodiscard]] bool persistApplicationSettings(const config::ApplicationSettings &settings);
    [[nodiscard]] bool saveHostProfileInternal(const QString &id, const QString &name, const QString &host, int port,
                                               const QString &username, const QString &authentication,
                                               const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                               const QString &group, const QString &secret, bool rememberCredential,
                                               bool manageCredential);
    void setCredentialOperationError(QString message);
    [[nodiscard]] bool startSshConnection(ssh::SshConnectionRequest request, QString sourceProfileId = {});
    void recordRecentConnection(TerminalTab &tab);
    [[nodiscard]] TerminalTab *activeTab();
    [[nodiscard]] const TerminalTab *activeTab() const;
    [[nodiscard]] TerminalTab *findTab(const QString &id);
    [[nodiscard]] const TerminalTab *findTab(const QString &id) const;
    void showActiveTab();

    static constexpr std::size_t maximumTerminalTabs = 32;

    ui::TerminalItem *m_terminal = nullptr;
    LocalTerminalSessionFactory m_localSessionFactory;
    ssh::SshProfileStore m_profileStore;
    config::ApplicationSettingsStore m_settingsStore;
    config::ApplicationSettings m_settings;
    std::unique_ptr<security::CredentialVaultCoordinator> m_credentialVaults;
    security::CredentialStorage m_defaultCredentialStorage = security::CredentialStorage::Session;
    QString m_credentialOperationError;
    QString m_knownHostsPath;
    std::vector<ssh::SshProfile> m_profiles;
    std::vector<std::unique_ptr<TerminalTab>> m_tabs;
    QString m_activeTabId;
    QString m_hostKeyTabId;
    QString m_hostKeyAlgorithm;
    QString m_hostKeyFingerprint;
    std::uint32_t m_nextLocalTabNumber = 1;
    bool m_hostKeyPromptVisible = false;
    bool m_hostKeyChangedWarning = false;
};

} // namespace ztermy
