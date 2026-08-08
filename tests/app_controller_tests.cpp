#include "application/AppController.h"
#include "infrastructure/security/PortableCredentialVault.h"
#include "infrastructure/ssh/SshProfileStore.h"
#include "platform/windows/WindowsCredentialVault.h"

#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <array>
#include <vector>

namespace
{

struct FakeLocalSessionState final
{
    int starts = 0;
    int stops = 0;
    QList<QByteArray> inputs;
    QList<QByteArray> pastes;
};

class FakeLocalTerminalSession final : public ztermy::terminal::LocalTerminalSessionBackend
{
public:
    explicit FakeLocalTerminalSession(std::shared_ptr<FakeLocalSessionState> state) : m_state(std::move(state)) {}

    [[nodiscard]] std::error_code start(const ztermy::terminal::TerminalGeometry) override
    {
        ++m_state->starts;
        m_running = true;
        emit statusChanged(QStringLiteral("Fake local terminal connected"));
        emit runningChanged(true);
        return {};
    }

    void stop() noexcept override
    {
        if (!m_running)
        {
            return;
        }
        m_running = false;
        ++m_state->stops;
        emit runningChanged(false);
    }

    void queueInput(const QByteArray &bytes) override { m_state->inputs.append(bytes); }
    void queuePaste(const QByteArray &bytes) override { m_state->pastes.append(bytes); }
    void requestResize(quint16, quint16, quint32, quint32) override {}
    void requestScroll(int) override {}
    void requestSelection(quint16, quint16, quint16, quint16, bool) override {}
    void clearSelection() override {}
    void copySelection() override {}
    void search(const QString &, bool, bool) override {}
    void clearSearch() override {}

private:
    std::shared_ptr<FakeLocalSessionState> m_state;
    bool m_running = false;
};

class WindowsCredentialCleanupGuard final
{
public:
    explicit WindowsCredentialCleanupGuard(ztermy::security::CredentialKey key) : m_key(std::move(key)) {}

    ~WindowsCredentialCleanupGuard()
    {
        ztermy::security::WindowsCredentialVault vault;
        const auto removed = vault.remove(m_key);
        (void)removed;
    }

    WindowsCredentialCleanupGuard(const WindowsCredentialCleanupGuard &) = delete;
    WindowsCredentialCleanupGuard &operator=(const WindowsCredentialCleanupGuard &) = delete;

private:
    ztermy::security::CredentialKey m_key;
};

} // namespace

class AppControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void savesUpdatesReloadsAndDeletesProfiles();
    void rejectsInvalidProfiles();
    void managesSavedCredentialsAndPortableVault();
    void sessionCredentialDoesNotAppearStoredAfterRestart();
    void installedControllerPersistsCredentialAcrossRestart();
    void portableControllerPersistsCredentialAcrossRestart();
    void saveAndConnectPersistsBeforeConnectionOutcome();
    void usesContextualSshTabTitles();
    void rejectsIncompleteConnections();
    void persistsApplicationSettings();
    void managesActionShortcutsAndDispatchContext();
    void managesMultipleLocalTerminalTabs();
    void orderlyShutdownStopsAllLocalTabsOnce();
    void persistsQuickCommandsAndPerTabWorkbenchState();
    void importsAndExportsScriptLibraryWithoutOverwritingIds();
    void loadsRecentProfilesAndParsesQuickTargets();
};

void AppControllerTests::savesUpdatesReloadsAndDeletesProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));

    ztermy::AppController controller(path);
    QSignalSpy changes(&controller, &ztermy::AppController::hostProfilesChanged);
    QVERIFY(controller.hostProfiles().isEmpty());

    QVERIFY(controller.saveHostProfile({}, QStringLiteral("Lab"), QStringLiteral("server.example.test"), 22,
                                       QStringLiteral("developer"), QStringLiteral("private-key"),
                                       QStringLiteral(R"(C:\keys\id_ed25519)"), true, QStringLiteral("Development")));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(controller.hostProfiles().size(), 1);

    const QVariantMap saved = controller.hostProfiles().constFirst().toMap();
    const QString id = saved.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(saved.value(QStringLiteral("name")).toString(), QStringLiteral("Lab"));
    QCOMPARE(saved.value(QStringLiteral("group")).toString(), QStringLiteral("Development"));
    QCOMPARE(saved.value(QStringLiteral("host")).toString(), QStringLiteral("server.example.test"));
    QCOMPARE(saved.value(QStringLiteral("port")).toInt(), 22);
    QCOMPARE(saved.value(QStringLiteral("authentication")).toString(), QStringLiteral("private-key"));
    QCOMPARE(saved.value(QStringLiteral("privateKeyPassphraseRequired")).toBool(), true);

    QVERIFY(controller.saveHostProfile(id, QStringLiteral("Updated"), QStringLiteral("new.example.test"), 2222,
                                       QStringLiteral("operator"), QStringLiteral("password"), {}, false,
                                       QStringLiteral("Production")));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(controller.hostProfiles().size(), 1);
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Updated"));
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("authentication")).toString(),
             QStringLiteral("password"));
    QVERIFY(
        controller.hostProfiles().constFirst().toMap().value(QStringLiteral("privateKeyPath")).toString().isEmpty());
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("group")).toString(),
             QStringLiteral("Production"));

    QVERIFY(controller.duplicateHostProfile(id));
    QCOMPARE(changes.count(), 3);
    QCOMPARE(controller.hostProfiles().size(), 2);
    const QVariantMap duplicate = controller.hostProfiles().constLast().toMap();
    QVERIFY(duplicate.value(QStringLiteral("id")).toString() != id);
    QCOMPARE(duplicate.value(QStringLiteral("name")).toString(), QStringLiteral("Updated copy"));
    QCOMPARE(duplicate.value(QStringLiteral("group")).toString(), QStringLiteral("Production"));
    QVERIFY(controller.duplicateHostProfile(id));
    QCOMPARE(controller.hostProfiles().constLast().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Updated copy 2"));
    const QString duplicateId = duplicate.value(QStringLiteral("id")).toString();
    const QString secondDuplicateId =
        controller.hostProfiles().constLast().toMap().value(QStringLiteral("id")).toString();

    ztermy::AppController reloaded(path);
    QCOMPARE(reloaded.hostProfiles(), controller.hostProfiles());

    QVERIFY(reloaded.deleteHostProfile(id));
    QVERIFY(reloaded.deleteHostProfile(duplicateId));
    QVERIFY(reloaded.deleteHostProfile(secondDuplicateId));
    QVERIFY(reloaded.hostProfiles().isEmpty());
    QVERIFY(!reloaded.deleteHostProfile(id));

    ztermy::AppController emptyReload(path);
    QVERIFY(emptyReload.hostProfiles().isEmpty());
}

void AppControllerTests::rejectsInvalidProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));
    QSignalSpy changes(&controller, &ztermy::AppController::hostProfilesChanged);

    QVERIFY(controller.saveHostProfile({}, {}, QStringLiteral("host"), 22, QStringLiteral("user"),
                                       QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("host"));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), {}, 22, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 0, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, {},
                                        QStringLiteral("private-key"), {}, false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, QStringLiteral("user"),
                                        QStringLiteral("agent"), {}, false, {}));
    QVERIFY(!controller.duplicateHostProfile(QStringLiteral("missing")));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(controller.hostProfiles().size(), 1);
}

void AppControllerTests::managesSavedCredentialsAndPortableVault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));
    const QString id = QStringLiteral("credential-profile");

    QVERIFY(controller.saveHostProfileWithCredential(id, {}, QStringLiteral("server.example.test"), 22,
                                                     QStringLiteral("developer"), QStringLiteral("password"), {}, false,
                                                     QStringLiteral("Lab"), QStringLiteral("saved-password"), true));
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
    QCOMPARE(controller.readHostCredential(id), QStringLiteral("saved-password"));
    QVERIFY(controller.credentialOperationError().isEmpty());

    QVERIFY(controller.duplicateHostProfile(id));
    QCOMPARE(controller.hostProfiles().at(1).toMap().value(QStringLiteral("credentialStored")).toBool(), false);
    QVERIFY(controller.forgetHostCredential(id));
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), false);

    QVERIFY(controller.saveHostProfileWithCredential(id, {}, QStringLiteral("server.example.test"), 22,
                                                     QStringLiteral("developer"), QStringLiteral("password"), {}, false,
                                                     QStringLiteral("Lab"), QStringLiteral("replacement"), true));
    QVERIFY(controller.initializePortableCredentialVault(QStringLiteral("correct horse battery staple")));
    QVERIFY(controller.migrateCredentialStorage(QStringLiteral("portable"), true));
    QCOMPARE(controller.effectiveCredentialStorage(), QStringLiteral("portable"));
    controller.lockPortableCredentialVault();
    QVERIFY(controller.portableVaultLocked());
    QVERIFY(controller.readHostCredential(id).isEmpty());
    QVERIFY(controller.credentialOperationError().contains(QStringLiteral("Unlock"), Qt::CaseInsensitive));
    QVERIFY(!controller.connectHostProfile(id, {}));
    QVERIFY(!controller.unlockPortableCredentialVault(QStringLiteral("wrong password")));
    QVERIFY(controller.unlockPortableCredentialVault(QStringLiteral("correct horse battery staple")));
    QCOMPARE(controller.readHostCredential(id), QStringLiteral("replacement"));
    QVERIFY(controller.deleteHostProfile(id));
}

void AppControllerTests::sessionCredentialDoesNotAppearStoredAfterRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString id = QStringLiteral("session-credential-profile");

    {
        ztermy::AppController controller(profilesPath);
        QVERIFY(controller.saveHostProfileWithCredential(
            id, {}, QStringLiteral("server.example.test"), 22, QStringLiteral("developer"), QStringLiteral("password"),
            {}, false, QStringLiteral("Lab"), QStringLiteral("session-password"), true));
        QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
    }

    ztermy::AppController reopened(profilesPath);
    QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), false);
    QVERIFY(reopened.saveHostCredential(id, QStringLiteral("replacement-session-password")));
    QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
}

void AppControllerTests::installedControllerPersistsCredentialAcrossRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const QString credentialsPath = directory.filePath(QStringLiteral("credentials.zvlt"));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const ztermy::security::CredentialKey key{.profileId = id.toStdString(),
                                              .kind = ztermy::security::CredentialKind::Password};
    const WindowsCredentialCleanupGuard cleanup(key);

    {
        ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                         ztermy::config::StorageMode::installed);
        const bool saved = controller.saveHostProfileWithCredential(
            id, {}, QStringLiteral("server.example.test"), 22, QStringLiteral("developer"), QStringLiteral("password"),
            {}, false, QStringLiteral("Lab"), QStringLiteral("unverified-password"), true);
        if (!saved
            && controller.credentialOperationError().contains(QStringLiteral("unavailable"), Qt::CaseInsensitive))
        {
            QSKIP("Windows Credential Manager is unavailable in this logon session");
        }
        QVERIFY(saved);
        QCOMPARE(controller.effectiveCredentialStorage(), QStringLiteral("system"));
        QVERIFY(controller.saveHostCredential(id, QStringLiteral("intentionally-wrong-password")));
    }

    ztermy::security::WindowsCredentialVault vault;
    auto persisted = vault.read(key);
    QVERIFY(persisted);
    QCOMPARE(QByteArray(persisted->view().data(), static_cast<qsizetype>(persisted->view().size())),
             QByteArrayLiteral("intentionally-wrong-password"));

    ztermy::AppController reopened(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                   ztermy::config::StorageMode::installed);
    QCOMPARE(reopened.effectiveCredentialStorage(), QStringLiteral("system"));
    QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
    QVERIFY(reopened.initializePortableCredentialVault(QStringLiteral("residual cleanup master password")));
    QVERIFY(reopened.migrateCredentialStorage(QStringLiteral("portable"), false));
    QVERIFY(reopened.clearCredentialStorage(QStringLiteral("system")));
    const auto clearedInactiveCopy = vault.read(key);
    QVERIFY(!clearedInactiveCopy);
    QCOMPARE(clearedInactiveCopy.error(), ztermy::security::CredentialVaultError::NotFound);
    QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
    QVERIFY(reopened.migrateCredentialStorage(QStringLiteral("system"), false));
    QVERIFY(reopened.forgetHostCredential(id));
    QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), false);
    const auto removed = vault.read(key);
    QVERIFY(!removed);
    QCOMPARE(removed.error(), ztermy::security::CredentialVaultError::NotFound);
}

void AppControllerTests::portableControllerPersistsCredentialAcrossRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const QString credentialsPath = directory.filePath(QStringLiteral("credentials.zvlt"));
    const QString id = QStringLiteral("portable-controller-profile");

    {
        ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                         ztermy::config::StorageMode::portable);
        QCOMPARE(controller.effectiveCredentialStorage(), QStringLiteral("portable"));
        QVERIFY(!controller.portableVaultInitialized());
        QVERIFY(controller.clearCredentialStorage(QStringLiteral("portable")));
        QVERIFY(controller.migrateCredentialStorage(QStringLiteral("session"), true));
        QCOMPARE(controller.effectiveCredentialStorage(), QStringLiteral("session"));
        QCOMPARE(controller.credentialStoragePreference(), QStringLiteral("session"));
        QVERIFY(controller.migrateCredentialStorage(QStringLiteral("portable"), true));
        QCOMPARE(controller.effectiveCredentialStorage(), QStringLiteral("portable"));
        QVERIFY(!controller.saveHostProfileWithCredential(
            id, {}, QStringLiteral("server.example.test"), 22, QStringLiteral("developer"), QStringLiteral("password"),
            {}, false, QStringLiteral("Lab"), QStringLiteral("unverified-portable-secret"), true));
        QVERIFY(controller.credentialOperationError().contains(QStringLiteral("Create the portable credential vault")));
        QVERIFY(controller.hostProfiles().isEmpty());
        QVERIFY(controller.initializePortableCredentialVault(QStringLiteral("original portable password")));
        QVERIFY(controller.saveHostProfileWithCredential(
            id, {}, QStringLiteral("server.example.test"), 22, QStringLiteral("developer"), QStringLiteral("password"),
            {}, false, QStringLiteral("Lab"), QStringLiteral("unverified-portable-secret"), true));
    }

    {
        ztermy::AppController reopened(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                       ztermy::config::StorageMode::portable);
        QVERIFY(reopened.portableVaultInitialized());
        QVERIFY(reopened.portableVaultLocked());
        QCOMPARE(reopened.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
        QVERIFY(!reopened.unlockPortableCredentialVault(QStringLiteral("wrong portable password")));
        QVERIFY(reopened.portableVaultLocked());
        QVERIFY(reopened.unlockPortableCredentialVault(QStringLiteral("original portable password")));
        QVERIFY(reopened.changePortableVaultMasterPassword(QStringLiteral("replacement portable password")));
    }

    ztermy::AppController finalController(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                          ztermy::config::StorageMode::portable);
    QVERIFY(!finalController.unlockPortableCredentialVault(QStringLiteral("original portable password")));
    QVERIFY(finalController.unlockPortableCredentialVault(QStringLiteral("replacement portable password")));
    QVERIFY(finalController.clearCredentialStorage(QStringLiteral("portable")));
    QCOMPARE(finalController.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), false);
    QVERIFY(finalController.deleteHostProfile(id));
    QVERIFY(finalController.hostProfiles().isEmpty());

    ztermy::security::PortableCredentialVault audit(credentialsPath);
    QVERIFY(audit.unlock(ztermy::security::SensitiveByteArray(QByteArrayLiteral("replacement portable password"))));
    QVERIFY(audit.listKeys()->empty());
}

void AppControllerTests::saveAndConnectPersistsBeforeConnectionOutcome()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));

    QVERIFY(controller.saveAndConnectHostProfile(
        {}, {}, QStringLiteral("127.0.0.1"), 1, QStringLiteral("connection-test"), QStringLiteral("password"), {},
        false, QStringLiteral("Tests"), QStringLiteral("pre-auth-secret"), true));
    QCOMPARE(controller.hostProfiles().size(), 1);
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("127.0.0.1"));
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(!controller.terminalTabs().isEmpty()
                                 && controller.terminalTabs().front().toMap().value(QStringLiteral("failed")).toBool(),
                             5000);
    QCOMPARE(controller.hostProfiles().front().toMap().value(QStringLiteral("credentialStored")).toBool(), true);
    controller.shutdown();
}

void AppControllerTests::usesContextualSshTabTitles()
{
    QTemporaryDir savedDirectory;
    QVERIFY(savedDirectory.isValid());
    ztermy::AppController savedController(savedDirectory.filePath(QStringLiteral("profiles.json")));

    QVERIFY(savedController.saveAndConnectHostProfile(
        {}, QStringLiteral("Production gateway"), QStringLiteral("127.0.0.1"), 1, QStringLiteral("saved-user"),
        QStringLiteral("password"), {}, false, QStringLiteral("Tests"), QStringLiteral("pre-auth-secret"), false));
    QVERIFY(!savedController.terminalTabs().isEmpty());
    QCOMPARE(savedController.terminalTabs().constFirst().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Production gateway"));
    savedController.shutdown();

    QTemporaryDir quickDirectory;
    QVERIFY(quickDirectory.isValid());
    ztermy::AppController quickController(quickDirectory.filePath(QStringLiteral("profiles.json")));

    QVERIFY(quickController.connectQuick(QStringLiteral("quick-user@127.0.0.1:1"), QStringLiteral("password"), {},
                                         false, QStringLiteral("pre-auth-secret"), false, {}, {}));
    QVERIFY(!quickController.terminalTabs().isEmpty());
    QCOMPARE(quickController.terminalTabs().constFirst().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("quick-user@127.0.0.1"));
    quickController.shutdown();
}

void AppControllerTests::rejectsIncompleteConnections()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));

    QVERIFY(
        !controller.connectPrivateKey(QStringLiteral("host"), 22, QStringLiteral("   "), QStringLiteral("key"), {}));
    QVERIFY(
        !controller.connectPrivateKey(QStringLiteral("host"), 22, QStringLiteral("user"), QStringLiteral("   "), {}));
    QVERIFY(!controller.connectPassword(QStringLiteral("host"), 22, QStringLiteral("user"), {}));
    QVERIFY(!controller.connectHostProfile(QStringLiteral("missing"), QStringLiteral("unused")));
}

void AppControllerTests::loadsRecentProfilesAndParsesQuickTargets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const std::array profiles{
        ztermy::ssh::SshProfile{
            .id = "older",
            .name = "Older host",
            .group = "Lab",
            .host = "older.example.test",
            .username = "alice",
            .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
            .privateKeyPath = "key",
            .lastConnectedUtcMs = 1000,
        },
        ztermy::ssh::SshProfile{
            .id = "newer",
            .name = "Newer host",
            .group = "Work",
            .host = "newer.example.test",
            .username = "bob",
            .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
            .lastConnectedUtcMs = 2000,
        },
        ztermy::ssh::SshProfile{
            .id = "never",
            .name = "Never connected",
            .group = "lab",
            .host = "never.example.test",
            .username = "carol",
            .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
            .privateKeyPath = "key",
        },
    };
    const ztermy::ssh::SshProfileStore store(path);
    QVERIFY(store.save(profiles));

    ztermy::AppController controller(path);
    const QVariantList recent = controller.recentHostProfiles();
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("newer"));
    QCOMPARE(recent.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("older"));
    QCOMPARE(recent.at(0).toMap().value(QStringLiteral("lastConnectedUtcMs")).toLongLong(), 2000);
    QCOMPARE(controller.hostProfileGroups(), QStringList({QStringLiteral("Lab"), QStringLiteral("Work")}));

    const QVariantMap parsed = controller.parseQuickConnectTarget(QStringLiteral("dora@[2001:db8::1]:2222"));
    QVERIFY(parsed.value(QStringLiteral("valid")).toBool());
    QCOMPARE(parsed.value(QStringLiteral("username")).toString(), QStringLiteral("dora"));
    QCOMPARE(parsed.value(QStringLiteral("host")).toString(), QStringLiteral("2001:db8::1"));
    QCOMPARE(parsed.value(QStringLiteral("port")).toInt(), 2222);

    const QVariantMap invalid = controller.parseQuickConnectTarget(QStringLiteral("dora@2001:db8::1"));
    QVERIFY(!invalid.value(QStringLiteral("valid")).toBool());
    QVERIFY(invalid.value(QStringLiteral("error")).toString().contains(QStringLiteral("brackets")));

    QVERIFY(controller.saveHostProfile(QStringLiteral("newer"), QStringLiteral("Renamed"),
                                       QStringLiteral("newer.example.test"), 22, QStringLiteral("bob"),
                                       QStringLiteral("password"), {}, false, {}));
    QCOMPARE(controller.recentHostProfiles().constFirst().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("newer"));
    QVERIFY(controller.duplicateHostProfile(QStringLiteral("newer")));
    QCOMPARE(controller.recentHostProfiles().size(), 2);
    QVERIFY(controller.setHostSectionCollapsed(QStringLiteral("recent"), true));
    QVERIFY(controller.setHostSectionCollapsed(QStringLiteral("group:Lab"), true));
    QCOMPARE(controller.collapsedHostSections(), QStringList({QStringLiteral("recent"), QStringLiteral("group:Lab")}));
    QVERIFY(controller.setHostSectionCollapsed(QStringLiteral("recent"), false));
    QCOMPARE(controller.collapsedHostSections(), QStringList({QStringLiteral("group:Lab")}));
    QVERIFY(controller.clearRecentHostProfiles());
    QVERIFY(controller.recentHostProfiles().isEmpty());

    std::vector<ztermy::ssh::SshProfile> cappedProfiles;
    cappedProfiles.reserve(8);
    for (std::int64_t index = 0; index < 8; ++index)
    {
        cappedProfiles.push_back({
            .id = "capped-" + std::to_string(index),
            .name = "Capped " + std::to_string(index),
            .host = "capped.example.test",
            .username = "tester",
            .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
            .lastConnectedUtcMs = 10'000 + index,
        });
    }
    QVERIFY(store.save(cappedProfiles));
    ztermy::AppController cappedController(path);
    const QVariantList cappedRecent = cappedController.recentHostProfiles();
    QCOMPARE(cappedRecent.size(), 6);
    QCOMPARE(cappedRecent.constFirst().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("capped-7"));
    QCOMPARE(cappedRecent.constLast().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("capped-2"));
}

void AppControllerTests::persistsApplicationSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));

    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath);
    QSignalSpy settingsChanged(&controller, &ztermy::AppController::applicationSettingsChanged);
    QCOMPARE(controller.themePreference(), QStringLiteral("dark"));
    QCOMPARE(controller.backdropOpacity(), 1.0);
    QCOMPARE(controller.backdropPreference(), QStringLiteral("acrylic"));
    QCOMPARE(controller.accentPreference(), QStringLiteral("ztermy"));
    QCOMPARE(controller.customAccent(), QStringLiteral("#22C55E"));
    QVERIFY(controller.uiFontFamily().isEmpty());
    QVERIFY(!controller.showAllTerminalFonts());
    QVERIFY(controller.terminalLigatures());
    QCOMPARE(controller.terminalBackgroundOpacity(), 1.0);
    QCOMPARE(controller.languagePreference(), QStringLiteral("system"));

    QVERIFY(controller.saveApplicationSettings(
        QStringLiteral("light"), 0.8, QStringLiteral("micaAlt"), QStringLiteral("custom"), QStringLiteral("#3366cc"),
        QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Cascadia Code"), 18, true, false, 0.45,
        QStringLiteral("bar"), false, true, false, QStringLiteral("zh_CN"), true, false));
    QCOMPARE(settingsChanged.count(), 1);
    QCOMPARE(controller.themePreference(), QStringLiteral("light"));
    QCOMPARE(controller.backdropOpacity(), 0.8);
    QCOMPARE(controller.backdropPreference(), QStringLiteral("micaAlt"));
    QCOMPARE(controller.accentPreference(), QStringLiteral("custom"));
    QCOMPARE(controller.customAccent(), QStringLiteral("#3366CC"));
    QCOMPARE(controller.uiFontFamily(), QStringLiteral("Microsoft YaHei UI"));
    QCOMPARE(controller.terminalFontFamily(), QStringLiteral("Cascadia Code"));
    QCOMPARE(controller.terminalFontSize(), 18);
    QVERIFY(controller.showAllTerminalFonts());
    QVERIFY(!controller.terminalLigatures());
    QCOMPARE(controller.terminalBackgroundOpacity(), 0.45);
    QCOMPARE(controller.cursorPreference(), QStringLiteral("bar"));
    QVERIFY(!controller.cursorBlink());
    QVERIFY(controller.copyOnSelect());
    QVERIFY(!controller.confirmMultilinePaste());
    QVERIFY(controller.sftpShowHiddenFiles());
    QVERIFY(!controller.sftpConfirmDelete());
    QCOMPARE(controller.languagePreference(), QStringLiteral("zh_CN"));

    QVERIFY(!controller.saveApplicationSettings(
        QStringLiteral("unknown"), 0.8, QStringLiteral("mica"), QStringLiteral("system"), QStringLiteral("#3366CC"), {},
        QStringLiteral("Cascadia Code"), 18, false, true, 0.45, QStringLiteral("bar"), false, true, false,
        QStringLiteral("zh_CN"), false, true));
    QVERIFY(!controller.saveApplicationSettings(
        QStringLiteral("light"), 0.8, QStringLiteral("mica"), QStringLiteral("system"), QStringLiteral("#3366CC"), {},
        QStringLiteral("Cascadia Code"), 18, false, true, 0.45, QStringLiteral("bar"), false, true, false,
        QStringLiteral("unsupported"), false, true));
    QVERIFY(!controller.saveApplicationSettings(
        QStringLiteral("light"), 0.8, QStringLiteral("mica"), QStringLiteral("custom"), QStringLiteral("invalid"), {},
        QStringLiteral("Cascadia Code"), 18, false, true, 0.45, QStringLiteral("bar"), false, true, false,
        QStringLiteral("zh_CN"), false, true));
    QCOMPARE(settingsChanged.count(), 1);

    ztermy::AppController reloaded(profilesPath, knownHostsPath, settingsPath);
    QCOMPARE(reloaded.themePreference(), QStringLiteral("light"));
    QCOMPARE(reloaded.backdropOpacity(), 0.8);
    QCOMPARE(reloaded.backdropPreference(), QStringLiteral("micaAlt"));
    QCOMPARE(reloaded.accentPreference(), QStringLiteral("custom"));
    QCOMPARE(reloaded.customAccent(), QStringLiteral("#3366CC"));
    QCOMPARE(reloaded.uiFontFamily(), QStringLiteral("Microsoft YaHei UI"));
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Cascadia Code"));
    QVERIFY(reloaded.showAllTerminalFonts());
    QVERIFY(!reloaded.terminalLigatures());
    QCOMPARE(reloaded.terminalBackgroundOpacity(), 0.45);
    QVERIFY(reloaded.copyOnSelect());
    QVERIFY(reloaded.sftpShowHiddenFiles());
    QVERIFY(!reloaded.sftpConfirmDelete());
    QCOMPARE(reloaded.languagePreference(), QStringLiteral("zh_CN"));

    QVERIFY(reloaded.resetApplicationSettings());
    QCOMPARE(reloaded.themePreference(), QStringLiteral("dark"));
    QCOMPARE(reloaded.backdropOpacity(), 1.0);
    QCOMPARE(reloaded.backdropPreference(), QStringLiteral("acrylic"));
    QCOMPARE(reloaded.accentPreference(), QStringLiteral("ztermy"));
    QCOMPARE(reloaded.customAccent(), QStringLiteral("#22C55E"));
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Cascadia Mono"));
    QCOMPARE(reloaded.terminalBackgroundOpacity(), 1.0);
    QCOMPARE(reloaded.languagePreference(), QStringLiteral("system"));
}

void AppControllerTests::managesActionShortcutsAndDispatchContext()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    const auto factory = [sessionState] {
        return std::make_unique<FakeLocalTerminalSession>(sessionState);
    };

    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, factory);
    QSignalSpy registryChanged(&controller, &ztermy::AppController::actionRegistryChanged);
    QSignalSpy actionRequested(&controller, &ztermy::AppController::actionRequested);

    const auto findAction = [&controller] {
        for (const QVariant &entry : controller.actions())
        {
            const QVariantMap action = entry.toMap();
            if (action.value(QStringLiteral("id")) == QStringLiteral("terminal.find"))
            {
                return action;
            }
        }
        return QVariantMap{};
    };

    QVERIFY(!findAction().value(QStringLiteral("enabled")).toBool());
    QVERIFY(!controller.triggerAction(QStringLiteral("terminal.find")));
    QVERIFY(controller.triggerAction(QStringLiteral("application.hosts")));
    QCOMPARE(actionRequested.count(), 1);
    QCOMPARE(actionRequested.constFirst().constFirst().toString(), QStringLiteral("application.hosts"));

    const QVariantMap conflict =
        controller.setActionShortcut(QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(!conflict.value(QStringLiteral("valid")).toBool());
    QVERIFY(!conflict.value(QStringLiteral("error")).toString().isEmpty());

    const QVariantMap changed =
        controller.setActionShortcut(QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Alt+F"));
    QVERIFY(changed.value(QStringLiteral("valid")).toBool());
    QCOMPARE(findAction().value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Alt+F"));
    QVERIFY(registryChanged.count() >= 1);

    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QVERIFY(findAction().value(QStringLiteral("enabled")).toBool());
    QVERIFY(controller.triggerAction(QStringLiteral("terminal.find")));
    QCOMPARE(actionRequested.constLast().constFirst().toString(), QStringLiteral("terminal.find"));

    ztermy::AppController reloaded(profilesPath, knownHostsPath, settingsPath, factory);
    QVariantMap reloadedFind;
    for (const QVariant &entry : reloaded.actions())
    {
        const QVariantMap action = entry.toMap();
        if (action.value(QStringLiteral("id")) == QStringLiteral("terminal.find"))
        {
            reloadedFind = action;
            break;
        }
    }
    QCOMPARE(reloadedFind.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Alt+F"));
    QVERIFY(reloaded.resetActionShortcut(QStringLiteral("terminal.find")));
    for (const QVariant &entry : reloaded.actions())
    {
        const QVariantMap action = entry.toMap();
        if (action.value(QStringLiteral("id")) == QStringLiteral("terminal.find"))
        {
            QCOMPARE(action.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Shift+F"));
        }
    }
}

void AppControllerTests::managesMultipleLocalTerminalTabs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });
    QSignalSpy tabsChanged(&controller, &ztermy::AppController::terminalTabsChanged);
    QSignalSpy activeChanged(&controller, &ztermy::AppController::activeTerminalTabChanged);
    QCOMPARE(controller.activeRemoteTelemetry().value(QStringLiteral("state")).toString(), QStringLiteral("paused"));
    QVERIFY(!controller.activeRemoteTelemetry().value(QStringLiteral("available")).toBool());

    const QString first = controller.startLocalTerminal();
    QVERIFY(!first.isEmpty());
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.activeTerminalTabId(), first);
    controller.setTerminalTelemetryVisible(true);
    controller.refreshRemoteTelemetry();
    QCOMPARE(controller.activeRemoteTelemetry().value(QStringLiteral("state")).toString(), QStringLiteral("paused"));
    QVERIFY(!controller.activeRemoteTelemetry().value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("connected")).toBool());
    QVERIFY(controller.transferTasks().isEmpty());
    QCOMPARE(controller.activeTransferCount(), 0);
    QVERIFY(!controller.toggleTerminalWorkbench(QStringLiteral("sftp")));
    QVERIFY(!controller.enqueueSftpUpload(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("upload.txt"))).toString()));
    QVERIFY(!controller.enqueueSftpDownload(
        QStringLiteral("/remote.txt"), QUrl::fromLocalFile(directory.filePath(QStringLiteral("remote.txt"))).toString(),
        1));
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchPage")).toString(),
             QStringLiteral("history"));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchSide")).toString(),
             QStringLiteral("left"));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toDouble(), 520.0);
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerHeight")).toDouble(), 132.0);
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("history")));
    controller.setTerminalWorkbenchWidth(700.0);
    controller.moveTerminalWorkbench();
    controller.toggleTerminalComposer();
    controller.setTerminalComposerHeight(240.0);
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchSide")).toString(),
             QStringLiteral("right"));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toDouble(), 700.0);
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerHeight")).toDouble(), 240.0);
    QVERIFY(controller.insertTerminalCommand(QStringLiteral("Get-Date")));
    QCOMPARE(sessionState->pastes.constLast(), QByteArray("Get-Date"));
    QVERIFY(controller.runTerminalCommand(QStringLiteral("Get-Date")));
    QCOMPARE(sessionState->inputs.constLast(), QByteArray("Get-Date\r"));
    const QVariantList firstTabHistory = controller.terminalHistory();
    QVERIFY(!firstTabHistory.isEmpty());
    QCOMPARE(firstTabHistory.constFirst().toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("Get-Date"));
    QVERIFY(controller.runTerminalCommand(QStringLiteral("Write-Output one\r\nWrite-Output two")));
    QCOMPARE(sessionState->inputs.constLast(), QByteArray("Write-Output one\rWrite-Output two\r"));
    QVERIFY(!controller.runTerminalCommand(QStringLiteral("  \n  ")));
    QVERIFY(!controller.runTerminalCommand(QString::fromLatin1("echo\x1b[2J")));
    QVERIFY(!controller.insertTerminalCommand(QString(65537, QLatin1Char('x'))));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("local"));
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("running")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("connecting")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("failed")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("remoteClosed")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Fake local terminal connected"));
    controller.retranslateUiState();
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Local PowerShell connected"));

    const QString second = controller.startLocalTerminal();
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QCOMPARE(controller.terminalTabs().size(), 2);
    QCOMPARE(controller.activeTerminalTabId(), second);
    QVERIFY(!controller.terminalTabs().at(1).toMap().value(QStringLiteral("workbenchOpen")).toBool());
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("scripts")));
    QCOMPARE(controller.terminalTabs().at(1).toMap().value(QStringLiteral("workbenchPage")).toString(),
             QStringLiteral("scripts"));
    QVERIFY(controller.runTerminalCommand(QStringLiteral("Write-Output second-tab")));
    const QVariantList globalHistory = controller.terminalGlobalHistory();
    QVERIFY(globalHistory.size() >= 3);
    const QVariantMap newestGlobalEntry = globalHistory.constFirst().toMap();
    QCOMPARE(newestGlobalEntry.value(QStringLiteral("command")).toString(), QStringLiteral("Write-Output second-tab"));
    QCOMPARE(newestGlobalEntry.value(QStringLiteral("sourceId")).toString(), second);
    QVERIFY(!newestGlobalEntry.value(QStringLiteral("sourceLabel")).toString().isEmpty());

    QVERIFY(controller.activateTerminalTab(first));
    QCOMPARE(controller.activeTerminalTabId(), first);
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchSide")).toString(),
             QStringLiteral("right"));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toDouble(), 700.0);
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerOpen")).toBool());
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("composerHeight")).toDouble(), 240.0);
    controller.closeTerminalWorkbench();
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchOpen")).toBool());
    QVERIFY(!controller.activateTerminalTab(QStringLiteral("missing")));

    QVERIFY(controller.closeTerminalTab(first));
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.activeTerminalTabId(), second);
    QVERIFY(controller.closeTerminalTab(second));
    QVERIFY(controller.terminalTabs().isEmpty());
    QVERIFY(controller.activeTerminalTabId().isEmpty());
    QVERIFY(!controller.closeTerminalTab(second));
    QCOMPARE(sessionState->starts, 2);
    QCOMPARE(sessionState->stops, 2);
    QVERIFY(tabsChanged.count() >= 4);
    QVERIFY(activeChanged.count() >= 4);
}

void AppControllerTests::orderlyShutdownStopsAllLocalTabsOnce()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });

    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QCOMPARE(sessionState->starts, 2);

    controller.shutdown();
    QCOMPARE(sessionState->stops, 2);
    QVERIFY(controller.terminalTabs().isEmpty());
    QVERIFY(controller.activeTerminalTabId().isEmpty());
    QVERIFY(controller.transferTasks().isEmpty());

    controller.shutdown();
    QCOMPARE(sessionState->stops, 2);
}

void AppControllerTests::persistsQuickCommandsAndPerTabWorkbenchState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));

    QString firstId;
    {
        ztermy::AppController controller(profilesPath);
        QSignalSpy changes(&controller, &ztermy::AppController::quickCommandsChanged);
        QVERIFY(controller.quickCommands().isEmpty());
        QVERIFY(controller.saveQuickCommand({}, QStringLiteral("List services"), QStringLiteral("systemctl --failed"),
                                            QStringLiteral("Failed services"), QStringLiteral("posix")));
        QVERIFY(controller.saveQuickCommand({}, QStringLiteral("PowerShell version"),
                                            QStringLiteral("$PSVersionTable.PSVersion"), {},
                                            QStringLiteral("powershell")));
        QCOMPARE(controller.quickCommands().size(), 2);
        firstId = controller.quickCommands().constFirst().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());
        QVERIFY(controller.saveQuickCommand(firstId, QStringLiteral("List failed services"),
                                            QStringLiteral("systemctl --failed\r\n"), QStringLiteral("Updated"),
                                            QStringLiteral("posix")));
        QCOMPARE(controller.quickCommands().constFirst().toMap().value(QStringLiteral("command")).toString(),
                 QStringLiteral("systemctl --failed\n"));
        QVERIFY(controller.moveQuickCommand(firstId, 1));
        QCOMPARE(controller.quickCommands().constLast().toMap().value(QStringLiteral("id")).toString(), firstId);
        QVERIFY(changes.count() >= 4);

        QVERIFY(!controller.saveQuickCommand({}, QStringLiteral("Unsafe"), QStringLiteral("echo\x1b[2J"), {},
                                             QStringLiteral("any")));
        QVERIFY(!controller.quickCommandOperationError().isEmpty());
    }

    ztermy::AppController reloaded(profilesPath);
    QCOMPARE(reloaded.quickCommands().size(), 2);
    QCOMPARE(reloaded.quickCommands().constLast().toMap().value(QStringLiteral("id")).toString(), firstId);
    QVERIFY(reloaded.deleteQuickCommand(firstId));
    QCOMPARE(reloaded.quickCommands().size(), 1);
}

void AppControllerTests::importsAndExportsScriptLibraryWithoutOverwritingIds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceProfiles = directory.filePath(QStringLiteral("source/profiles.json"));
    const QString targetProfiles = directory.filePath(QStringLiteral("target/profiles.json"));
    const QString libraryPath = directory.filePath(QStringLiteral("library.json"));
    const QString libraryUrl = QUrl::fromLocalFile(libraryPath).toString();

    ztermy::AppController source(sourceProfiles);
    QVERIFY(source.saveQuickCommand({}, QStringLiteral("Disk usage"), QStringLiteral("df -h"),
                                    QStringLiteral("Show mounted filesystems"), QStringLiteral("posix")));
    QVERIFY(source.saveQuickCommand({}, QStringLiteral("Processes"), QStringLiteral("Get-Process"), {},
                                    QStringLiteral("powershell")));
    QVERIFY(source.exportQuickCommands(libraryUrl));

    QVERIFY(source.importQuickCommands(libraryUrl));
    QCOMPARE(source.quickCommands().size(), 4);
    QSet<QString> sourceIds;
    for (const QVariant &value : source.quickCommands())
    {
        sourceIds.insert(value.toMap().value(QStringLiteral("id")).toString());
    }
    QCOMPARE(sourceIds.size(), 4);

    ztermy::AppController target(targetProfiles);
    QVERIFY(target.importQuickCommands(libraryUrl));
    QCOMPARE(target.quickCommands().size(), 2);
    QCOMPARE(target.quickCommands().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Disk usage"));
    QVERIFY(target.quickCommandOperationError().isEmpty());
}

QTEST_GUILESS_MAIN(AppControllerTests)

#include "app_controller_tests.moc"
