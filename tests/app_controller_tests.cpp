#include "application/AppController.h"
#include "infrastructure/ssh/SshProfileStore.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <array>
#include <vector>

namespace
{

struct FakeLocalSessionState final
{
    int starts = 0;
    int stops = 0;
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

    void queueInput(const QByteArray &) override {}
    void queuePaste(const QByteArray &) override {}
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

} // namespace

class AppControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void savesUpdatesReloadsAndDeletesProfiles();
    void rejectsInvalidProfiles();
    void rejectsIncompleteConnections();
    void persistsApplicationSettings();
    void managesMultipleLocalTerminalTabs();
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

    QVERIFY(!controller.saveHostProfile({}, {}, QStringLiteral("host"), 22, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), {}, 22, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 0, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, {},
                                        QStringLiteral("private-key"), {}, false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, QStringLiteral("user"),
                                        QStringLiteral("agent"), {}, false, {}));
    QVERIFY(!controller.duplicateHostProfile(QStringLiteral("missing")));
    QCOMPARE(changes.count(), 0);
    QVERIFY(controller.hostProfiles().isEmpty());
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
            .host = "older.example.test",
            .username = "alice",
            .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
            .privateKeyPath = "key",
            .lastConnectedUtcMs = 1000,
        },
        ztermy::ssh::SshProfile{
            .id = "newer",
            .name = "Newer host",
            .host = "newer.example.test",
            .username = "bob",
            .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
            .lastConnectedUtcMs = 2000,
        },
        ztermy::ssh::SshProfile{
            .id = "never",
            .name = "Never connected",
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
    QCOMPARE(controller.terminalBackgroundOpacity(), 1.0);

    QVERIFY(controller.saveApplicationSettings(QStringLiteral("light"), 0.8, QStringLiteral("micaAlt"),
                                               QStringLiteral("Cascadia Code"), 18, 0.45, QStringLiteral("bar"), false,
                                               true, false));
    QCOMPARE(settingsChanged.count(), 1);
    QCOMPARE(controller.themePreference(), QStringLiteral("light"));
    QCOMPARE(controller.backdropOpacity(), 0.8);
    QCOMPARE(controller.backdropPreference(), QStringLiteral("micaAlt"));
    QCOMPARE(controller.terminalFontFamily(), QStringLiteral("Cascadia Code"));
    QCOMPARE(controller.terminalFontSize(), 18);
    QCOMPARE(controller.terminalBackgroundOpacity(), 0.45);
    QCOMPARE(controller.cursorPreference(), QStringLiteral("bar"));
    QVERIFY(!controller.cursorBlink());
    QVERIFY(controller.copyOnSelect());
    QVERIFY(!controller.confirmMultilinePaste());

    QVERIFY(!controller.saveApplicationSettings(QStringLiteral("unknown"), 0.8, QStringLiteral("mica"),
                                                QStringLiteral("Cascadia Code"), 18, 0.45, QStringLiteral("bar"), false,
                                                true, false));
    QCOMPARE(settingsChanged.count(), 1);

    ztermy::AppController reloaded(profilesPath, knownHostsPath, settingsPath);
    QCOMPARE(reloaded.themePreference(), QStringLiteral("light"));
    QCOMPARE(reloaded.backdropOpacity(), 0.8);
    QCOMPARE(reloaded.backdropPreference(), QStringLiteral("micaAlt"));
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Cascadia Code"));
    QCOMPARE(reloaded.terminalBackgroundOpacity(), 0.45);
    QVERIFY(reloaded.copyOnSelect());

    QVERIFY(reloaded.resetApplicationSettings());
    QCOMPARE(reloaded.themePreference(), QStringLiteral("dark"));
    QCOMPARE(reloaded.backdropOpacity(), 1.0);
    QCOMPARE(reloaded.backdropPreference(), QStringLiteral("acrylic"));
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Cascadia Mono"));
    QCOMPARE(reloaded.terminalBackgroundOpacity(), 1.0);
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

    const QString first = controller.startLocalTerminal();
    QVERIFY(!first.isEmpty());
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.activeTerminalTabId(), first);
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("local"));
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("running")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("connecting")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("failed")).toBool());
    QVERIFY(!controller.terminalTabs().constFirst().toMap().value(QStringLiteral("remoteClosed")).toBool());

    const QString second = controller.startLocalTerminal();
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QCOMPARE(controller.terminalTabs().size(), 2);
    QCOMPARE(controller.activeTerminalTabId(), second);

    QVERIFY(controller.activateTerminalTab(first));
    QCOMPARE(controller.activeTerminalTabId(), first);
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

QTEST_GUILESS_MAIN(AppControllerTests)

#include "app_controller_tests.moc"
