#include "application/AppController.h"
#include "application/ai/AiConversationHistoryModel.h"
#include "application/ai/AiConversationModel.h"
#include "core/config/ApplicationSettings.h"
#include "infrastructure/security/PortableCredentialVault.h"
#include "infrastructure/ssh/SshProfileStore.h"
#include "infrastructure/workbench/WorkspaceStateStore.h"
#include "platform/windows/WindowsCredentialVault.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <array>
#include <ranges>
#include <span>
#include <vector>

namespace
{

struct FakeLocalSessionState final
{
    int starts = 0;
    int stops = 0;
    QList<QByteArray> inputs;
    std::vector<ztermy::terminal::TerminalKeyEvent> keyEvents;
    std::vector<ztermy::terminal::TerminalMouseEvent> mouseEvents;
    std::vector<bool> focusEvents;
    QList<QByteArray> pastes;
    QString selectedText;
    std::vector<std::string> scrollbackLines{"fake scrollback line"};
    std::size_t scrollbackLineCount = 0;
    std::shared_ptr<ztermy::terminal::TerminalOutputSink> outputSink;
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
    void queueKeyEvent(const ztermy::terminal::TerminalKeyEvent &event) override
    {
        m_state->keyEvents.push_back(event);
    }
    void queueMouseEvent(const ztermy::terminal::TerminalMouseEvent &event) override
    {
        m_state->mouseEvents.push_back(event);
    }
    void queueFocusEvent(const bool focused) override { m_state->focusEvents.push_back(focused); }
    void queuePaste(const QByteArray &bytes) override { m_state->pastes.append(bytes); }
    void setOutputSink(const std::shared_ptr<ztermy::terminal::TerminalOutputSink> &sink) override
    {
        m_state->outputSink = sink;
    }
    void requestResize(quint16, quint16, quint32, quint32) override {}
    void requestScroll(int) override {}
    void requestSelection(quint16, quint16, quint16, quint16, bool) override {}
    void requestSelectionGesture(const ztermy::terminal::TerminalSelectionGesture &) override {}
    void requestCopyModeAction(const ztermy::terminal::TerminalCopyModeAction &) override {}
    void selectAll() override {}
    void clearSelection() override {}
    void copySelection() override {}
    void requestSelectedText() override { emit selectedTextReady(m_state->selectedText); }
    void search(const QString &, bool, bool) override {}
    void clearSearch() override {}
    [[nodiscard]] std::expected<ztermy::terminal::TerminalScrollbackPage, std::error_code>
    scrollbackPage(const ztermy::terminal::TerminalScrollbackRequest request) const override
    {
        ztermy::terminal::TerminalScrollbackPage page;
        page.totalLines = m_state->scrollbackLines.size();
        page.scrollbackLines = std::min(m_state->scrollbackLineCount, page.totalLines);
        std::size_t end;
        if (request.anchor == ztermy::terminal::TerminalScrollbackAnchor::head)
        {
            page.firstLine = request.offset;
            end = page.firstLine < page.totalLines
                      ? page.firstLine + std::min(request.lineCount, page.totalLines - page.firstLine)
                      : page.firstLine;
        }
        else
        {
            end = request.offset < page.totalLines ? page.totalLines - request.offset : 0;
            page.firstLine = end > request.lineCount ? end - request.lineCount : 0;
        }
        for (std::size_t index = page.firstLine; index < end && index < page.totalLines; ++index)
        {
            page.lines.push_back(m_state->scrollbackLines[index]);
        }
        return page;
    }

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
    void persistsAndPreservesSessionOptions();
    void managesExplicitProxyProfilesAndCredentials();
    void managesOrderedJumpHostProfiles();
    void managesPortForwardingRules();
    void rejectsInvalidProfiles();
    void agentProfilesNeverStoreCredentials();
    void managesSavedCredentialsAndPortableVault();
    void sessionCredentialDoesNotAppearStoredAfterRestart();
    void installedControllerPersistsCredentialAcrossRestart();
    void portableControllerPersistsCredentialAcrossRestart();
    void saveAndConnectPersistsBeforeConnectionOutcome();
    void usesContextualSshTabTitles();
    void rejectsIncompleteConnections();
    void persistsApplicationSettings();
    void refreshesConfiguredAiModelsAtStartup();
    void refreshesConfiguredAiModelsInBackground();
    void clearsAiModelCatalogWhenProviderChangesOrChatGptSignsOut();
    void routesAiModelDiscoveryThroughCustomProxy();
    void managesMcpServerConfiguration();
    void managesActionShortcutsAndDispatchContext();
    void restoresCompleteAgentPresentationFromHistory();
    void exposesProviderFailureRecoveryActions();
    void retriesProviderResponseWithoutRepeatingCompletedTool();
    void managesMultipleLocalTerminalTabs();
    void managesFreshTerminalTabWorkflows();
    void managesPersistentTerminalWorkspaceSplits();
    void restoresSavedSshWorkspaceWithoutConnecting();
    void managesSessionAppearanceAndStructuredRecording();
    void orderlyShutdownStopsAllLocalTabsOnce();
    void exposesAndDismissesStartupRecoveryNotice();
    void persistsQuickCommandsAndPerTabWorkbenchState();
    void persistsAiQuickMessages();
    void scansAndExposesAiUserSkills();
    void importsAndExportsScriptLibraryWithoutOverwritingIds();
    void rendersAndRunsScriptAgainstFixedTerminal();
    void managesLocalMarkdownNotesAndLatestSearch();
    void loadsRecentProfilesAndParsesQuickTargets();
    void reconnectsSavedKeyProfileOnRealHost();
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

void AppControllerTests::managesPortForwardingRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));

    ztermy::AppController controller(path);
    QVERIFY(controller.saveHostProfile({}, QStringLiteral("Forward host"), QStringLiteral("host.example.test"), 22,
                                       QStringLiteral("operator"), QStringLiteral("agent"), {}, false, {}));
    const QString profileId = controller.hostProfiles().constFirst().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!profileId.isEmpty());

    QSignalSpy changes(&controller, &ztermy::AppController::portForwardingRulesChanged);
    QVERIFY(controller.savePortForwardingRule({}, QStringLiteral("Database"), profileId, QStringLiteral("local"),
                                              QStringLiteral("127.0.0.1"), 15432, QStringLiteral("database.internal"),
                                              5432, false));
    QVERIFY(changes.count() >= 1);
    QCOMPARE(controller.portForwardingRules().size(), 1);
    const QVariantMap local = controller.portForwardingRules().constFirst().toMap();
    const QString localId = local.value(QStringLiteral("id")).toString();
    QVERIFY(!localId.isEmpty());
    QCOMPARE(local.value(QStringLiteral("profileName")).toString(), QStringLiteral("Forward host"));
    QCOMPARE(local.value(QStringLiteral("type")).toString(), QStringLiteral("local"));
    QCOMPARE(local.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));

    QVERIFY(controller.savePortForwardingRule({}, QStringLiteral("SOCKS"), profileId, QStringLiteral("dynamic"),
                                              QStringLiteral("127.0.0.1"), 11080, {}, 0, true));
    QCOMPARE(controller.portForwardingRules().size(), 2);
    const QString dynamicId =
        controller.portForwardingRules().constLast().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!dynamicId.isEmpty());

    QVERIFY(controller.duplicatePortForwardingRule(dynamicId));
    QCOMPARE(controller.portForwardingRules().size(), 3);
    const QVariantMap duplicate = controller.portForwardingRules().constLast().toMap();
    const QString duplicateId = duplicate.value(QStringLiteral("id")).toString();
    QVERIFY(!duplicateId.isEmpty());
    QVERIFY(duplicateId != dynamicId);
    QCOMPARE(duplicate.value(QStringLiteral("label")).toString(), QStringLiteral("SOCKS copy"));
    QCOMPARE(duplicate.value(QStringLiteral("autoStart")).toBool(), false);

    QVERIFY(!controller.savePortForwardingRule({}, QStringLiteral("Invalid"), profileId, QStringLiteral("remote"),
                                               QStringLiteral("127.0.0.1"), 2200, {}, 0, false));
    QVERIFY(!controller.portForwardingOperationError().isEmpty());
    QVERIFY(!controller.deleteHostProfile(profileId));

    ztermy::AppController reloaded(path);
    QCOMPARE(reloaded.portForwardingRules().size(), 3);
    QCOMPARE(reloaded.portForwardingRules().at(1).toMap().value(QStringLiteral("autoStart")).toBool(), true);

    QVERIFY(controller.deletePortForwardingRule(localId));
    QVERIFY(controller.deletePortForwardingRule(dynamicId));
    QVERIFY(controller.deletePortForwardingRule(duplicateId));
    QVERIFY(controller.deleteHostProfile(profileId));
}

void AppControllerTests::persistsAndPreservesSessionOptions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    ztermy::AppController controller(path);

    const QVariantMap options{
        {QStringLiteral("terminalType"), QStringLiteral("screen-256color")},
        {QStringLiteral("connectionTimeoutSeconds"), 25},
        {QStringLiteral("keepaliveIntervalSeconds"), 45},
        {QStringLiteral("keepaliveFailureThreshold"), 4},
        {QStringLiteral("startupCommand"), QStringLiteral("uname -a")},
        {QStringLiteral("startupCommandMode"), QStringLiteral("line-delay")},
        {QStringLiteral("startupLineDelayMilliseconds"), 125},
        {QStringLiteral("reconnectPolicy"), QStringLiteral("transport-failure")},
        {QStringLiteral("reconnectMaximumAttempts"), 5},
        {QStringLiteral("reconnectInitialBackoffMilliseconds"), 750},
        {QStringLiteral("environment"),
         QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("LANG")},
                                  {QStringLiteral("value"), QStringLiteral("C.UTF-8")}}}},
    };
    QVERIFY(controller.saveHostProfileWithCredential(
        {}, QStringLiteral("Advanced"), QStringLiteral("server.example.test"), 22, QStringLiteral("operator"),
        QStringLiteral("password"), {}, false, QStringLiteral("Lab"), {}, false, options));

    const QVariantMap saved = controller.hostProfiles().constFirst().toMap();
    const QString id = saved.value(QStringLiteral("id")).toString();
    const QVariantMap savedOptions = saved.value(QStringLiteral("sessionOptions")).toMap();
    QCOMPARE(savedOptions.value(QStringLiteral("terminalType")).toString(), QStringLiteral("screen-256color"));
    QCOMPARE(savedOptions.value(QStringLiteral("connectionTimeoutSeconds")).toInt(), 25);
    QCOMPARE(savedOptions.value(QStringLiteral("keepaliveIntervalSeconds")).toInt(), 45);
    QCOMPARE(savedOptions.value(QStringLiteral("startupCommandMode")).toString(), QStringLiteral("line-delay"));
    QCOMPARE(savedOptions.value(QStringLiteral("reconnectPolicy")).toString(), QStringLiteral("transport-failure"));
    QCOMPARE(savedOptions.value(QStringLiteral("reconnectMaximumAttempts")).toInt(), 5);
    QCOMPARE(
        savedOptions.value(QStringLiteral("environment")).toList().constFirst().toMap().value(QStringLiteral("value")),
        QStringLiteral("C.UTF-8"));

    QVERIFY(controller.saveHostProfile(id, QStringLiteral("Renamed"), QStringLiteral("server.example.test"), 22,
                                       QStringLiteral("operator"), QStringLiteral("password"), {}, false,
                                       QStringLiteral("Lab")));
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("sessionOptions")).toMap(),
             savedOptions);

    QVariantMap invalid = options;
    invalid.insert(QStringLiteral("connectionTimeoutSeconds"), 0);
    QVERIFY(!controller.saveHostProfileWithCredential(
        id, QStringLiteral("Invalid"), QStringLiteral("server.example.test"), 22, QStringLiteral("operator"),
        QStringLiteral("password"), {}, false, QStringLiteral("Lab"), {}, false, invalid));
    invalid = options;
    invalid.insert(QStringLiteral("keepaliveIntervalSeconds"), 4000);
    QVERIFY(!controller.saveHostProfileWithCredential(
        id, QStringLiteral("Invalid"), QStringLiteral("server.example.test"), 22, QStringLiteral("operator"),
        QStringLiteral("password"), {}, false, QStringLiteral("Lab"), {}, false, invalid));
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Renamed"));

    ztermy::AppController reloaded(path);
    QCOMPARE(reloaded.hostProfiles().constFirst().toMap().value(QStringLiteral("sessionOptions")).toMap(),
             savedOptions);
}

void AppControllerTests::managesExplicitProxyProfilesAndCredentials()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    ztermy::AppController controller(profilesPath);
    const QVariantMap proxy{{QStringLiteral("type"), QStringLiteral("socks5")},
                            {QStringLiteral("host"), QStringLiteral("proxy.example.test")},
                            {QStringLiteral("port"), 1080},
                            {QStringLiteral("username"), QStringLiteral("proxy-user")}};

    QVERIFY(controller.saveHostProfileWithCredential(
        QStringLiteral("proxied"), QStringLiteral("Proxied host"), QStringLiteral("server.example.test"), 22,
        QStringLiteral("operator"), QStringLiteral("password"), {}, false, QStringLiteral("Lab"),
        QStringLiteral("host-secret"), true, {}, proxy, QStringLiteral("proxy-secret"), true));
    QVariantMap saved = controller.hostProfiles().constFirst().toMap();
    const QVariantMap savedProxy = saved.value(QStringLiteral("proxy")).toMap();
    QCOMPARE(savedProxy.value(QStringLiteral("type")).toString(), QStringLiteral("socks5"));
    QCOMPARE(savedProxy.value(QStringLiteral("host")).toString(), QStringLiteral("proxy.example.test"));
    QCOMPARE(savedProxy.value(QStringLiteral("port")).toInt(), 1080);
    QCOMPARE(savedProxy.value(QStringLiteral("username")).toString(), QStringLiteral("proxy-user"));
    QVERIFY(savedProxy.value(QStringLiteral("credentialStored")).toBool());
    QCOMPARE(controller.readHostCredential(QStringLiteral("proxied")), QStringLiteral("host-secret"));
    QCOMPARE(controller.readProxyCredential(QStringLiteral("proxied")), QStringLiteral("proxy-secret"));

    QVERIFY(controller.saveProxyCredential(QStringLiteral("proxied"), QStringLiteral("updated-proxy-secret")));
    QCOMPARE(controller.readProxyCredential(QStringLiteral("proxied")), QStringLiteral("updated-proxy-secret"));

    QVERIFY(controller.saveHostProfileWithCredential(QStringLiteral("proxied"), QStringLiteral("Proxied host"),
                                                     QStringLiteral("server.example.test"), 22,
                                                     QStringLiteral("operator"), QStringLiteral("password"), {}, false,
                                                     QStringLiteral("Lab"), {}, true, {}, proxy, {}, true));
    saved = controller.hostProfiles().constFirst().toMap();
    QVERIFY(saved.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("credentialStored")).toBool());

    const QVariantMap direct{{QStringLiteral("type"), QStringLiteral("none")}};
    QVERIFY(controller.saveHostProfileWithCredential(QStringLiteral("proxied"), QStringLiteral("Direct host"),
                                                     QStringLiteral("server.example.test"), 22,
                                                     QStringLiteral("operator"), QStringLiteral("password"), {}, false,
                                                     QStringLiteral("Lab"), {}, true, {}, direct, {}, false));
    saved = controller.hostProfiles().constFirst().toMap();
    QCOMPARE(saved.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("none"));
    QVERIFY(!saved.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("credentialStored")).toBool());

    QFile persisted(profilesPath);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    const QByteArray contents = persisted.readAll();
    QVERIFY(!contents.contains("host-secret"));
    QVERIFY(!contents.contains("proxy-secret"));
    QVERIFY(!contents.contains("updated-proxy-secret"));
}

void AppControllerTests::managesOrderedJumpHostProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    ztermy::AppController controller(profilesPath);

    QVERIFY(controller.saveHostProfile(QStringLiteral("jump-agent"), QStringLiteral("Edge"),
                                       QStringLiteral("edge.example.test"), 22, QStringLiteral("operator"),
                                       QStringLiteral("agent"), {}, false, QStringLiteral("Gateways")));
    QVERIFY(controller.saveHostProfileWithCredential(QStringLiteral("jump-password"), QStringLiteral("Inner"),
                                                     QStringLiteral("inner.example.test"), 2222,
                                                     QStringLiteral("developer"), QStringLiteral("password"), {}, false,
                                                     QStringLiteral("Gateways"), QStringLiteral("jump-secret"), true));

    const QVariantMap route{
        {QStringLiteral("jumpProfileIds"), QVariantList{QStringLiteral("jump-agent"), QStringLiteral("jump-password")}},
    };
    QVERIFY(controller.saveHostProfileWithCredential(QStringLiteral("target"), QStringLiteral("Target"),
                                                     QStringLiteral("target.example.test"), 22, QStringLiteral("root"),
                                                     QStringLiteral("agent"), {}, false, QStringLiteral("Servers"), {},
                                                     false, {}, {}, {}, false, route));

    const QVariantList profiles = controller.hostProfiles();
    const auto targetPosition = std::ranges::find(profiles, QStringLiteral("target"), [](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString();
    });
    QVERIFY(targetPosition != profiles.end());
    const QVariantMap target = targetPosition->toMap();
    QCOMPARE(target.value(QStringLiteral("jumpProfileIds")).toList(),
             QVariantList({QStringLiteral("jump-agent"), QStringLiteral("jump-password")}));
    const QVariantList jumps = target.value(QStringLiteral("jumpProfiles")).toList();
    QCOMPARE(jumps.size(), 2);
    QCOMPARE(jumps.constFirst().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Edge"));
    QVERIFY(target.value(QStringLiteral("jumpProfilesReady")).toBool());
    QVERIFY(target.value(QStringLiteral("connectionCredentialStored")).toBool());

    QVERIFY(!controller.deleteHostProfile(QStringLiteral("jump-agent")));
    QVERIFY(controller.credentialOperationError().contains(QStringLiteral("Target")));
    QVERIFY(!controller.saveHostProfileWithCredential(
        QStringLiteral("target"), QStringLiteral("Target"), QStringLiteral("target.example.test"), 22,
        QStringLiteral("root"), QStringLiteral("agent"), {}, false, QStringLiteral("Servers"), {}, false, {}, {}, {},
        false, QVariantMap{{QStringLiteral("jumpProfileIds"), QVariantList{QStringLiteral("missing")}}}));

    QVERIFY(controller.saveHostProfileWithCredential(
        QStringLiteral("target"), QStringLiteral("Target"), QStringLiteral("target.example.test"), 22,
        QStringLiteral("root"), QStringLiteral("agent"), {}, false, QStringLiteral("Servers"), {}, false, {}, {}, {},
        false, QVariantMap{{QStringLiteral("jumpProfileIds"), QVariantList{}}}));
    QVERIFY(controller.deleteHostProfile(QStringLiteral("jump-agent")));

    ztermy::AppController reloaded(profilesPath);
    const QVariantList reloadedProfiles = reloaded.hostProfiles();
    const auto reloadedTarget =
        std::ranges::find(reloadedProfiles, QStringLiteral("target"), [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("id")).toString();
        });
    QVERIFY(reloadedTarget != reloadedProfiles.end());
    QVERIFY(reloadedTarget->toMap().value(QStringLiteral("jumpProfileIds")).toList().isEmpty());
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
    QVERIFY(controller.saveHostProfile({}, QStringLiteral("Agent"), QStringLiteral("agent-host"), 22,
                                       QStringLiteral("user"), QStringLiteral("agent"), {}, false, {}));
    QVERIFY(!controller.duplicateHostProfile(QStringLiteral("missing")));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(controller.hostProfiles().size(), 2);
}

void AppControllerTests::agentProfilesNeverStoreCredentials()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));
    const QString id = QStringLiteral("agent-profile");

    QVERIFY(controller.saveHostProfileWithCredential(
        id, QStringLiteral("Password profile"), QStringLiteral("server.example.test"), 22, QStringLiteral("developer"),
        QStringLiteral("password"), {}, false, QStringLiteral("Lab"), QStringLiteral("saved-password"), true));
    QCOMPARE(controller.readHostCredential(id), QStringLiteral("saved-password"));

    QVERIFY(controller.saveHostProfileWithCredential(
        id, QStringLiteral("Agent profile"), QStringLiteral("server.example.test"), 22, QStringLiteral("developer"),
        QStringLiteral("agent"), QStringLiteral("must-be-cleared"), true, QStringLiteral("Lab"),
        QStringLiteral("must-not-be-stored"), true));
    const QVariantMap profile = controller.hostProfiles().constFirst().toMap();
    QCOMPARE(profile.value(QStringLiteral("authentication")).toString(), QStringLiteral("agent"));
    QVERIFY(profile.value(QStringLiteral("privateKeyPath")).toString().isEmpty());
    QVERIFY(!profile.value(QStringLiteral("privateKeyPassphraseRequired")).toBool());
    QVERIFY(!profile.value(QStringLiteral("credentialStored")).toBool());
    QVERIFY(controller.readHostCredential(id).isEmpty());
    QVERIFY(!controller.saveHostCredential(id, QStringLiteral("must-not-be-stored")));
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
        QVERIFY(!controller.setAiConversationHistoryEnabled(true));
        QVERIFY(controller.initializePortableCredentialVault(QStringLiteral("original portable password")));
        QVERIFY(controller.setAiConversationHistoryEnabled(true));
        QVERIFY(controller.aiConversationHistoryEnabled());
        QFile historyEnvelope(directory.filePath(QStringLiteral("ai_conversations.enc")));
        QVERIFY(historyEnvelope.open(QIODevice::WriteOnly));
        QCOMPARE(historyEnvelope.write("encrypted-history-placeholder"), 29);
        historyEnvelope.close();
        QVERIFY(!controller.migrateCredentialStorage(QStringLiteral("session"), true));
        QVERIFY(controller.credentialOperationError().contains(QStringLiteral("Delete encrypted AI history")));
        QVERIFY(controller.saveHostProfileWithCredential(
            id, {}, QStringLiteral("server.example.test"), 22, QStringLiteral("developer"), QStringLiteral("password"),
            {}, false, QStringLiteral("Lab"), QStringLiteral("unverified-portable-secret"), true));
    }

    {
        ztermy::AppController reopened(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                       ztermy::config::StorageMode::portable);
        QVERIFY(reopened.portableVaultInitialized());
        QVERIFY(reopened.portableVaultLocked());
        QVERIFY(reopened.aiConversationHistoryEnabled());
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
    QCOMPARE(controller.aiProviderPreference(), QStringLiteral("openai-responses"));
    QVERIFY(controller.aiWebSearchAvailable());
    QVERIFY(!controller.aiChatGptConfigured());
    QCOMPARE(controller.aiChatGptAuthState(), QStringLiteral("signed-out"));
    QVERIFY(controller.aiChatGptAccountId().isEmpty());
    QVERIFY(controller.aiChatGptAuthError().isEmpty());
    QVERIFY(controller.aiChatGptDeviceCode().isEmpty());
    QCOMPARE(controller.aiBaseUrl(), QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(controller.aiEndpointPath().isEmpty());
    QVERIFY(controller.aiModel().isEmpty());
    QVERIFY(!controller.aiAutomaticContext());
    QCOMPARE(controller.aiPermissionPreference(), QStringLiteral("ask"));
    QCOMPARE(controller.aiProxyPreference(), QStringLiteral("system"));
    QVERIFY(controller.aiProxyUrl().isEmpty());
    QVERIFY(controller.aiProxyUsername().isEmpty());
    QVERIFY(!controller.aiProxyPasswordConfigured());
    QCOMPARE(controller.aiProviderEndpointPreview(
                 QStringLiteral("gemini"), QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/")),
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
    QCOMPARE(controller.aiProviderEndpointPreview(QStringLiteral("openrouter"),
                                                  QStringLiteral("https://openrouter.ai/api/v1")),
             QStringLiteral("https://openrouter.ai/api/v1/chat/completions"));
    QCOMPARE(controller.aiProviderEndpointPreview(QStringLiteral("qwen"),
                                                  QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1")),
             QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"));
    QCOMPARE(
        controller.aiReasoningCapabilities(QStringLiteral("gemini"), QStringLiteral("gemini-3.7-flash"))
            .value(QStringLiteral("tokens"))
            .toStringList(),
        QStringList({QStringLiteral("auto"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}));
    QCOMPARE(controller.aiReasoningCapabilities(QStringLiteral("openrouter"), QStringLiteral("openai/gpt-latest"))
                 .value(QStringLiteral("tokens"))
                 .toStringList(),
             QStringList({QStringLiteral("auto"), QStringLiteral("off"), QStringLiteral("low"),
                          QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}));
    QCOMPARE(controller.aiReasoningCapabilities(QStringLiteral("qwen"), QStringLiteral("qwen3.7-plus"))
                 .value(QStringLiteral("tokens"))
                 .toStringList(),
             QStringList({QStringLiteral("auto"), QStringLiteral("off")}));

    QVERIFY(controller.saveAiProviderConfiguration(
        QStringLiteral("openai-chatgpt"), QStringLiteral("https://chatgpt.com/backend-api/codex"),
        QStringLiteral("gpt-5.4"), false, QStringLiteral("ask"), {}, false, QStringLiteral("high")));
    QCOMPARE(controller.aiProviderPreference(), QStringLiteral("openai-chatgpt"));
    QVERIFY(controller.aiWebSearchAvailable());
    QVERIFY(controller.signOutAiChatGpt());
    QCOMPARE(controller.aiChatGptAuthState(), QStringLiteral("signed-out"));
    QVERIFY(controller.aiChatGptDeviceCode().isEmpty());

    QVERIFY(controller.saveAiProviderConfiguration(QStringLiteral("openai-compatible"),
                                                   QStringLiteral("https://gateway.example.test/v1"),
                                                   QStringLiteral("saved-model"), false, QStringLiteral("auto"),
                                                   QStringLiteral("test-api-key"), false, QStringLiteral("auto")));
    QCOMPARE(controller.aiProviderPreference(), QStringLiteral("openai-compatible"));
    QCOMPARE(controller.aiBaseUrl(), QStringLiteral("https://gateway.example.test/v1"));
    QCOMPARE(controller.aiModel(), QStringLiteral("saved-model"));
    QVERIFY(controller.aiApiKeyConfigured());

    QVERIFY(controller.saveAiProviderSettings(QStringLiteral("ollama"), QStringLiteral("http://127.0.0.1:11434"),
                                              QStringLiteral("/api/chat"), QStringLiteral("qwen3"), false,
                                              QStringLiteral("auto")));
    QCOMPARE(controller.aiProviderPreference(), QStringLiteral("ollama"));
    QVERIFY(!controller.aiWebSearchAvailable());
    QCOMPARE(controller.aiBaseUrl(), QStringLiteral("http://127.0.0.1:11434"));
    QCOMPARE(controller.aiEndpointPath(), QStringLiteral("/api/chat"));
    QCOMPARE(controller.aiModel(), QStringLiteral("qwen3"));
    QVERIFY(!controller.aiAutomaticContext());
    QCOMPARE(controller.aiPermissionPreference(), QStringLiteral("auto"));
    QVERIFY(!controller.saveAiProviderSettings(QStringLiteral("unknown"), QStringLiteral("https://example.test"), {},
                                               QStringLiteral("model"), true, QStringLiteral("read-only")));
    QVERIFY(!controller.saveAiProviderSettings(QStringLiteral("ollama"), QStringLiteral("file:///tmp/model"), {},
                                               QStringLiteral("model"), true, QStringLiteral("read-only")));
    QVERIFY(controller.saveAiProxySettings(QStringLiteral("custom"), QStringLiteral("http://127.0.0.1:7890"),
                                           QStringLiteral("proxy-user"), {}));
    QCOMPARE(controller.aiProxyPreference(), QStringLiteral("custom"));
    QCOMPARE(controller.aiProxyUrl(), QStringLiteral("http://127.0.0.1:7890"));
    QCOMPARE(controller.aiProxyUsername(), QStringLiteral("proxy-user"));
    QVERIFY(!controller.aiProxyPasswordConfigured());
    QVERIFY(!controller.saveAiProxySettings(QStringLiteral("invalid"), {}, {}, {}));
    settingsChanged.clear();

    QVERIFY(controller.saveApplicationSettings(
        QStringLiteral("light"), 0.8, QStringLiteral("micaAlt"), QStringLiteral("custom"), QStringLiteral("#3366cc"),
        QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Cascadia Code"), 18, true, false, 0.45,
        QStringLiteral("bar"), false, true, false, QStringLiteral("zh_CN"), true, false, true, true,
        QStringLiteral("copy-paste"), QStringLiteral("paste"), QStringLiteral(" |,"), 7));
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
    QCOMPARE(controller.terminalRightClickBehavior(), QStringLiteral("copy-paste"));
    QCOMPARE(controller.terminalMiddleClickBehavior(), QStringLiteral("paste"));
    QCOMPARE(controller.terminalWordDelimiters(), QStringLiteral(" |,"));
    QCOMPARE(controller.terminalScrollRows(), 7);
    QVERIFY(controller.sftpShowHiddenFiles());
    QVERIFY(!controller.sftpConfirmDelete());
    QVERIFY(controller.closeToTray());
    QVERIFY(controller.performanceMode());
    QCOMPARE(controller.languagePreference(), QStringLiteral("zh_CN"));
    QCOMPARE(controller.aiProviderPreference(), QStringLiteral("ollama"));
    QCOMPARE(controller.aiModel(), QStringLiteral("qwen3"));

    QVERIFY(!controller.saveApplicationSettings(
        QStringLiteral("unknown"), 0.8, QStringLiteral("mica"), QStringLiteral("system"), QStringLiteral("#3366CC"), {},
        QStringLiteral("Cascadia Code"), 18, false, true, 0.45, QStringLiteral("bar"), false, true, false,
        QStringLiteral("zh_CN"), false, true));
    QVERIFY(!controller.saveApplicationSettings(
        QStringLiteral("light"), 0.8, QStringLiteral("mica"), QStringLiteral("system"), QStringLiteral("#3366CC"), {},
        QStringLiteral("Cascadia Code"), 18, false, true, 0.45, QStringLiteral("bar"), false, true, false,
        QStringLiteral("zh_CN"), false, true, false, false, QStringLiteral("unsupported")));
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
    QCOMPARE(reloaded.terminalRightClickBehavior(), QStringLiteral("copy-paste"));
    QCOMPARE(reloaded.terminalMiddleClickBehavior(), QStringLiteral("paste"));
    QCOMPARE(reloaded.terminalWordDelimiters(), QStringLiteral(" |,"));
    QCOMPARE(reloaded.terminalScrollRows(), 7);
    QVERIFY(reloaded.sftpShowHiddenFiles());
    QVERIFY(!reloaded.sftpConfirmDelete());
    QVERIFY(reloaded.closeToTray());
    QVERIFY(reloaded.performanceMode());
    QCOMPARE(reloaded.languagePreference(), QStringLiteral("zh_CN"));
    QCOMPARE(reloaded.aiProviderPreference(), QStringLiteral("ollama"));
    QVERIFY(!reloaded.aiWebSearchAvailable());
    QCOMPARE(reloaded.aiBaseUrl(), QStringLiteral("http://127.0.0.1:11434"));
    QCOMPARE(reloaded.aiModel(), QStringLiteral("qwen3"));
    QVERIFY(!reloaded.aiAutomaticContext());
    QCOMPARE(reloaded.aiProxyPreference(), QStringLiteral("custom"));
    QCOMPARE(reloaded.aiProxyUrl(), QStringLiteral("http://127.0.0.1:7890"));
    QCOMPARE(reloaded.aiProxyUsername(), QStringLiteral("proxy-user"));

    QVERIFY(reloaded.resetApplicationSettings());
    QCOMPARE(reloaded.themePreference(), QStringLiteral("dark"));
    QCOMPARE(reloaded.backdropOpacity(), 1.0);
    QCOMPARE(reloaded.backdropPreference(), QStringLiteral("acrylic"));
    QVERIFY(!reloaded.performanceMode());
    QCOMPARE(reloaded.accentPreference(), QStringLiteral("ztermy"));
    QCOMPARE(reloaded.customAccent(), QStringLiteral("#22C55E"));
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Cascadia Mono"));
    QCOMPARE(reloaded.terminalBackgroundOpacity(), 1.0);
    QCOMPARE(reloaded.languagePreference(), QStringLiteral("system"));
    QVERIFY(!reloaded.closeToTray());
    QVERIFY(reloaded.aiWebSearchAvailable());
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

void AppControllerTests::refreshesConfiguredAiModelsAtStartup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer provider;
    QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
    int requestCount = 0;
    connect(&provider, &QTcpServer::newConnection, &provider, [&] {
        while (provider.hasPendingConnections())
        {
            QTcpSocket *socket = provider.nextPendingConnection();
            QVERIFY(socket != nullptr);
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, request, &requestCount] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n"))
                {
                    return;
                }
                ++requestCount;
                const QByteArray body = QByteArrayLiteral(R"({"models":[{"name":"qwen3"},{"name":"gemma3"}]})");
                QByteArray response = QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                response += QByteArray::number(body.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    auto settings = ztermy::config::ApplicationSettings{};
    settings.aiProvider = ztermy::config::AiProviderPreference::ollama;
    settings.aiBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(provider.serverPort());
    settings.aiModel = QStringLiteral("qwen3");
    QVERIFY(ztermy::config::ApplicationSettingsStore(settingsPath).save(settings));
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, [sessionState] {
        return std::make_unique<FakeLocalTerminalSession>(sessionState);
    });

    QTRY_COMPARE_WITH_TIMEOUT(controller.aiAvailableModels(),
                              QStringList({QStringLiteral("gemma3"), QStringLiteral("qwen3")}), 5'000);
    QCOMPARE(requestCount, 1);
    QVERIFY(controller.aiModelsError().isEmpty());
}

void AppControllerTests::refreshesConfiguredAiModelsInBackground()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer provider;
    QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
    int requestCount = 0;
    connect(&provider, &QTcpServer::newConnection, &provider, [&] {
        while (provider.hasPendingConnections())
        {
            QTcpSocket *socket = provider.nextPendingConnection();
            QVERIFY(socket != nullptr);
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, request, &requestCount] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n"))
                {
                    return;
                }
                ++requestCount;
                const bool fail = requestCount >= 4;
                const QByteArray body = fail ? QByteArrayLiteral(R"({"error":"temporary catalog failure"})")
                                             : QByteArrayLiteral(R"({"data":[{"id":"model-a"},{"id":"model-b"}]})");
                QByteArray response =
                    fail ? QByteArrayLiteral("HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                                             "application/json\r\nConnection: close\r\nContent-Length: ")
                         : QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                                             "close\r\nContent-Length: ");
                response += QByteArray::number(body.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, [sessionState] {
        return std::make_unique<FakeLocalTerminalSession>(sessionState);
    });
    const QString endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(provider.serverPort());
    QVERIFY(controller.saveAiProviderConfiguration(QStringLiteral("openai-compatible"), endpoint,
                                                   QStringLiteral("model-a"), false, QStringLiteral("auto"),
                                                   QStringLiteral("test-api-key"), false, QStringLiteral("auto")));
    QVERIFY(!controller.aiModelsLoading());

    QTRY_COMPARE_WITH_TIMEOUT(controller.aiAvailableModels(),
                              QStringList({QStringLiteral("model-a"), QStringLiteral("model-b")}), 5'000);
    QCOMPARE(requestCount, 1);
    QVERIFY(controller.aiModelsError().isEmpty());

    QSignalSpy modelsChanged(&controller, &ztermy::AppController::aiModelsChanged);
    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(modelsChanged.count() >= 1, 5'000);
    QCOMPARE(requestCount, 2);
    modelsChanged.clear();
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("ai")));
    QTRY_VERIFY_WITH_TIMEOUT(modelsChanged.count() >= 1, 5'000);
    QCOMPARE(requestCount, 3);
    QCOMPARE(controller.aiAvailableModels(), QStringList({QStringLiteral("model-a"), QStringLiteral("model-b")}));
    QVERIFY(controller.aiModelsError().isEmpty());

    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("ai")));
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("ai")));
    QTRY_COMPARE_WITH_TIMEOUT(requestCount, 4, 5'000);
    QTest::qWait(50);
    QCOMPARE(controller.aiAvailableModels(), QStringList({QStringLiteral("model-a"), QStringLiteral("model-b")}));
    QVERIFY(controller.aiModelsError().isEmpty());
}

void AppControllerTests::clearsAiModelCatalogWhenProviderChangesOrChatGptSignsOut()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer provider;
    QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
    connect(&provider, &QTcpServer::newConnection, &provider, [&] {
        while (provider.hasPendingConnections())
        {
            QTcpSocket *socket = provider.nextPendingConnection();
            QVERIFY(socket != nullptr);
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, request] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n"))
                {
                    return;
                }
                const QByteArray body = QByteArrayLiteral(R"({"data":[{"id":"stale-model"}]})");
                QByteArray response = QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                response += QByteArray::number(body.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, [sessionState] {
        return std::make_unique<FakeLocalTerminalSession>(sessionState);
    });
    const QString endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(provider.serverPort());

    controller.refreshAiModels(QStringLiteral("openai-compatible"), endpoint, QStringLiteral("test-api-key"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.aiAvailableModels(), QStringList({QStringLiteral("stale-model")}), 5'000);

    QVERIFY(controller.saveAiProviderConfiguration(
        QStringLiteral("openai-chatgpt"), QStringLiteral("https://chatgpt.com/backend-api/codex"),
        QStringLiteral("gpt-5.3-codex"), false, QStringLiteral("ask"), {}, false, QStringLiteral("auto")));
    QVERIFY(controller.aiAvailableModels().isEmpty());
    QVERIFY(!controller.aiModelsLoading());
    QVERIFY(controller.aiModelsError().isEmpty());

    controller.refreshAiModels(QStringLiteral("openai-compatible"), endpoint, QStringLiteral("test-api-key"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.aiAvailableModels(), QStringList({QStringLiteral("stale-model")}), 5'000);
    QVERIFY(controller.signOutAiChatGpt());
    QVERIFY(controller.aiAvailableModels().isEmpty());
    QVERIFY(!controller.aiModelsLoading());
    QVERIFY(controller.aiModelsError().isEmpty());
}

void AppControllerTests::routesAiModelDiscoveryThroughCustomProxy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer proxy;
    QVERIFY(proxy.listen(QHostAddress::LocalHost, 0));
    int requestCount = 0;
    QByteArray receivedRequest;
    connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        while (proxy.hasPendingConnections())
        {
            QTcpSocket *socket = proxy.nextPendingConnection();
            QVERIFY(socket != nullptr);
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, request, &requestCount, &receivedRequest] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n"))
                {
                    return;
                }
                ++requestCount;
                receivedRequest = *request;
                const QByteArray body = QByteArrayLiteral(R"({"data":[{"id":"proxy-model"}]})");
                QByteArray response = QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                response += QByteArray::number(body.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, [sessionState] {
        return std::make_unique<FakeLocalTerminalSession>(sessionState);
    });
    const QString proxyUrl = QStringLiteral("http://127.0.0.1:%1").arg(proxy.serverPort());
    QVERIFY(controller.saveAiProxySettings(QStringLiteral("custom"), proxyUrl, {}, {}));
    QVERIFY(controller.saveAiProviderConfiguration(QStringLiteral("openai-compatible"),
                                                   QStringLiteral("http://models.example.test/v1"),
                                                   QStringLiteral("proxy-model"), false, QStringLiteral("auto"),
                                                   QStringLiteral("test-api-key"), false, QStringLiteral("auto")));

    QTRY_COMPARE_WITH_TIMEOUT(controller.aiAvailableModels(), QStringList({QStringLiteral("proxy-model")}), 5'000);
    QCOMPARE(requestCount, 1);
    QVERIFY(receivedRequest.startsWith("GET http://models.example.test/v1/models "));
    QVERIFY(receivedRequest.toLower().contains("\r\nhost: models.example.test\r\n"));
    QVERIFY(controller.aiModelsError().isEmpty());
}

void AppControllerTests::managesMcpServerConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profiles = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHosts = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settings = directory.filePath(QStringLiteral("settings.json"));
    const auto factory = [] {
        return std::make_unique<FakeLocalTerminalSession>(std::make_shared<FakeLocalSessionState>());
    };

    {
        ztermy::AppController controller(profiles, knownHosts, settings, factory);
        QSignalSpy changes(&controller, &ztermy::AppController::mcpConfigurationChanged);
        QVERIFY(controller.saveMcpServer(QStringLiteral("local-test"), QStringLiteral("local_test"),
                                         QCoreApplication::applicationFilePath(), {}, {}, QStringLiteral("observe"),
                                         false));
        QVERIFY(changes.count() > 0);
        QCOMPARE(controller.mcpServers().size(), 1);
        const QVariantMap server = controller.mcpServers().constFirst().toMap();
        QCOMPARE(server.value(QStringLiteral("namespace")).toString(), QStringLiteral("local_test"));
        QCOMPARE(server.value(QStringLiteral("state")).toString(), QStringLiteral("disabled"));
        QVERIFY(controller.mcpTools().isEmpty());
        QVERIFY(controller.restartMcpServer(QStringLiteral("local-test")));
        QVERIFY(!controller.setMcpToolApproved(QStringLiteral("local-test"), QStringLiteral("missing"),
                                               QString(64, QLatin1Char('a')), true));
    }

    ztermy::AppController reloaded(profiles, knownHosts, settings, factory);
    QCOMPARE(reloaded.mcpServers().size(), 1);
    QVERIFY(reloaded.removeMcpServer(QStringLiteral("local-test")));
    QVERIFY(reloaded.mcpServers().isEmpty());
}

void AppControllerTests::restoresCompleteAgentPresentationFromHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(
        directory.filePath(QStringLiteral("profiles.json")), directory.filePath(QStringLiteral("known_hosts.json")),
        directory.filePath(QStringLiteral("settings.json")), directory.filePath(QStringLiteral("credentials.zvlt")),
        ztermy::config::StorageMode::portable, [sessionState] {
            return std::make_unique<FakeLocalTerminalSession>(sessionState);
        });
    QVERIFY(controller.initializePortableCredentialVault(QStringLiteral("agent history password")));
    QVERIFY(controller.setAiConversationHistoryEnabled(true));
    QVERIFY(!controller.startLocalTerminal().isEmpty());

    auto *history = qobject_cast<ztermy::ai::AiConversationHistoryModel *>(controller.aiConversationHistory());
    QVERIFY(history != nullptr);
    const QString conversationId = QStringLiteral("agent-presentation-history");
    ztermy::ai::AiStoredConversation stored{
        .id = conversationId,
        .title = QStringLiteral("Inspect disks"),
        .updatedAtUtc = QDateTime::currentDateTimeUtc(),
        .messages = {
            {.role = QStringLiteral("user"), .text = QStringLiteral("Inspect disk usage")},
            {.role = QStringLiteral("assistant"),
             .text = QStringLiteral("Disk usage is healthy."),
             .reasoning = QStringLiteral("Inspect the filesystem table first."),
             .toolActivities = {{.id = "tool-1",
                                 .name = "run_command",
                                 .summary = "df -h",
                                 .argumentsJson = R"({"command":"df -h"})",
                                 .resultJson = R"({"ok":true,"output":"disk table"})",
                                 .state = "succeeded",
                                 .resultCode = "ok",
                                 .sideEffecting = true}},
             .usage = ztermy::ai::AiTokenUsage{.inputTokens = 30, .outputTokens = 9, .reasoningTokens = 4},
             .metrics =
                 ztermy::ai::AiTurnMetrics{.wallTimeMilliseconds = 680, .firstTokenMilliseconds = 125, .retryCount = 1},
             .estimatedCostUsd = 0.00051,
             .costCatalogDate = QStringLiteral("2026-08-15"),
             .longContextRates = true}}};
    history->persist(stored);
    QTRY_COMPARE(history->rowCount(), 1);
    QTRY_VERIFY(!history->busy());

    QVERIFY(controller.restoreAiConversationHistory(conversationId));
    auto *conversation = qobject_cast<ztermy::ai::AiConversationModel *>(controller.activeAiConversation());
    QVERIFY(conversation != nullptr);
    QCOMPARE(conversation->rowCount(), 2);
    QCOMPARE(conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::ReasoningRole).toString(),
             QStringLiteral("Inspect the filesystem table first."));
    const QVariantList activities =
        conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::ToolActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("summary")).toString(), QStringLiteral("df -h"));
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("argumentsJson")).toString(),
             QStringLiteral(R"({"command":"df -h"})"));
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("resultJson")).toString(),
             QStringLiteral(R"({"ok":true,"output":"disk table"})"));
    QCOMPARE(conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::InputTokensRole).toULongLong(),
             qulonglong{30});
    QCOMPARE(conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::WallTimeMillisecondsRole)
                 .toULongLong(),
             qulonglong{680});
    QVERIFY(
        conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::EstimatedCostKnownRole).toBool());
}

void AppControllerTests::exposesProviderFailureRecoveryActions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")),
                                     directory.filePath(QStringLiteral("settings.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });
    QVERIFY(!controller.startLocalTerminal().isEmpty());

    QTcpServer provider;
    QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&provider, &QTcpServer::newConnection, &controller, [&provider] {
        while (QTcpSocket *socket = provider.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                QByteArray request = socket->property("requestBuffer").toByteArray();
                request += socket->readAll();
                socket->setProperty("requestBuffer", request);
                if (!request.contains("\r\n\r\n") || socket->property("responseSent").toBool())
                {
                    return;
                }
                socket->setProperty("responseSent", true);
                const QByteArray body =
                    R"({"error":{"message":"Invalid API key","type":"authentication_error","code":"invalid_api_key"}})";
                QByteArray response = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nConnection: "
                                      "close\r\nContent-Length: ";
                response += QByteArray::number(body.size());
                response += "\r\n\r\n";
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString endpoint = QStringLiteral("http://127.0.0.1:%1").arg(provider.serverPort());
    QVERIFY(controller.saveAiProviderSettings(QStringLiteral("ollama"), endpoint, QStringLiteral("/api/chat"),
                                              QStringLiteral("qwen3"), false, QStringLiteral("ask")));
    QVERIFY(controller.sendAiMessage(QStringLiteral("Summarize the current terminal.")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeAiState(), QStringLiteral("error"), 5'000);

    const QVariantMap recovery = controller.activeAiErrorRecovery();
    QCOMPARE(recovery.value(QStringLiteral("code")).toString(), QStringLiteral("authentication"));
    QVERIFY(recovery.value(QStringLiteral("messageAnchored")).toBool());
    QVERIFY(recovery.value(QStringLiteral("retryAvailable")).toBool());
    QVERIFY(recovery.value(QStringLiteral("settingsAvailable")).toBool());
    QVERIFY(!recovery.value(QStringLiteral("newConversationAvailable")).toBool());
    QVERIFY(recovery.value(QStringLiteral("hint")).toString().contains(QStringLiteral("API key")));

    controller.clearAiConversation();
    QCOMPARE(controller.activeAiState(), QStringLiteral("idle"));
    QVERIFY(controller.activeAiErrorRecovery().isEmpty());
}

void AppControllerTests::retriesProviderResponseWithoutRepeatingCompletedTool()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")),
                                     directory.filePath(QStringLiteral("settings.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });
    QVERIFY(!controller.startLocalTerminal().isEmpty());

    QTcpServer provider;
    QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
    QList<QByteArray> requestBodies;
    QObject::connect(&provider, &QTcpServer::newConnection, &controller, [&provider, &requestBodies] {
        while (QTcpSocket *socket = provider.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestBodies] {
                QByteArray request = socket->property("requestBuffer").toByteArray();
                request += socket->readAll();
                socket->setProperty("requestBuffer", request);
                if (socket->property("responseSent").toBool())
                {
                    return;
                }
                const qsizetype headerEnd = request.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                {
                    return;
                }
                qsizetype contentLength = 0;
                const QList<QByteArray> headerLines = request.first(headerEnd).split('\n');
                for (QByteArray line : headerLines)
                {
                    line = line.trimmed();
                    if (line.toLower().startsWith("content-length:"))
                    {
                        contentLength = line.sliced(qsizetype{15}).trimmed().toLongLong();
                        break;
                    }
                }
                const qsizetype bodyOffset = headerEnd + qsizetype{4};
                if (request.size() - bodyOffset < contentLength)
                {
                    return;
                }

                socket->setProperty("responseSent", true);
                if (request.startsWith("GET "))
                {
                    const QByteArray body = R"({"models":[{"name":"qwen3"}]})";
                    QByteArray response =
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ";
                    response += QByteArray::number(body.size());
                    response += "\r\n\r\n";
                    response += body;
                    socket->write(response);
                    socket->disconnectFromHost();
                    return;
                }
                requestBodies.push_back(request.sliced(bodyOffset, contentLength));
                QByteArray body;
                if (requestBodies.size() == 1)
                {
                    body =
                        R"({"message":{"content":"","tool_calls":[{"function":{"name":"run_command","arguments":{"command":"echo retry-once","timeout_ms":1}}}]},"done":true})"
                        "\n";
                }
                else if (requestBodies.size() == 2)
                {
                    body = R"({"error":"temporary upstream failure"})"
                           "\n";
                }
                else
                {
                    body =
                        R"({"message":{"content":"continued without rerunning"},"done":true,"prompt_eval_count":9,"eval_count":3})"
                        "\n";
                }
                QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n"
                                      "Content-Length: ";
                response += QByteArray::number(body.size());
                response += "\r\n\r\n";
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    const QString endpoint = QStringLiteral("http://127.0.0.1:%1").arg(provider.serverPort());
    QVERIFY(controller.saveAiProviderSettings(QStringLiteral("ollama"), endpoint, QStringLiteral("/api/chat"),
                                              QStringLiteral("qwen3"), false, QStringLiteral("yolo")));
    const auto commandDispatchCount = [&sessionState] {
        return std::ranges::count_if(sessionState->inputs, [](const QByteArray &input) {
            return input.trimmed() == QByteArrayLiteral("echo retry-once");
        });
    };
    QCOMPARE(commandDispatchCount(), 0);
    QVERIFY(controller.sendAiMessage(QStringLiteral("Inspect disk usage")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeAiState(), QStringLiteral("error"), 5'000);
    QCOMPARE(commandDispatchCount(), 1);

    QVERIFY(controller.retryAiMessage());
    QCOMPARE(commandDispatchCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeAiState(), QStringLiteral("complete"), 5'000);
    QCOMPARE(commandDispatchCount(), 1);
    QCOMPARE(requestBodies.size(), 3);

    const QJsonArray retryMessages =
        QJsonDocument::fromJson(requestBodies.at(2)).object().value(QStringLiteral("messages")).toArray();
    int originalPromptCount = 0;
    bool foundToolEvidence = false;
    bool foundRetryContinuation = false;
    for (const auto &value : retryMessages)
    {
        const QString content = value.toObject().value(QStringLiteral("content")).toString();
        originalPromptCount += content == QStringLiteral("Inspect disk usage") ? 1 : 0;
        foundToolEvidence = foundToolEvidence || content.contains(QStringLiteral("echo retry-once"));
        foundRetryContinuation =
            foundRetryContinuation || content.contains(QStringLiteral("do not repeat side-effecting actions"));
    }
    QCOMPARE(originalPromptCount, 1);
    QVERIFY(foundToolEvidence);
    QVERIFY(foundRetryContinuation);

    auto *conversation = qobject_cast<ztermy::ai::AiConversationModel *>(controller.activeAiConversation());
    QVERIFY(conversation != nullptr);
    QCOMPARE(conversation->rowCount(), 3);
    QCOMPARE(conversation->data(conversation->index(0), ztermy::ai::AiConversationModel::MessageRole).toString(),
             QStringLiteral("user"));
    QCOMPARE(conversation->data(conversation->index(1), ztermy::ai::AiConversationModel::StateRole).toString(),
             QStringLiteral("failed"));
    QCOMPARE(conversation->data(conversation->index(2), ztermy::ai::AiConversationModel::TextRole).toString(),
             QStringLiteral("continued without rerunning"));
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
    QVERIFY(controller.activeAiConversation() != nullptr);
    auto *aiConversation = qobject_cast<ztermy::ai::AiConversationModel *>(controller.activeAiConversation());
    QVERIFY(aiConversation != nullptr);
    QVERIFY(aiConversation->appendUserMessage(QStringLiteral("Show disk usage")));
    const auto assistantId = aiConversation->beginAssistantMessage();
    QVERIFY(assistantId != 0);
    QVERIFY(aiConversation->appendAssistantDelta(assistantId, QStringLiteral("Use `df -h`.")));
    QVERIFY(aiConversation->completeAssistantMessage(assistantId));
    const QString conversationExport = directory.filePath(QStringLiteral("conversation.md"));
    QVERIFY(controller.exportAiConversation(QUrl::fromLocalFile(conversationExport).toString()));
    QFile conversationFile(conversationExport);
    QVERIFY(conversationFile.open(QIODevice::ReadOnly));
    const QByteArray conversationMarkdown = conversationFile.readAll();
    QVERIFY(conversationMarkdown.contains("## User"));
    QVERIFY(conversationMarkdown.contains("Show disk usage"));
    QVERIFY(conversationMarkdown.contains("## Assistant"));
    QVERIFY(conversationMarkdown.contains("Use `df -h`."));
    QCOMPARE(controller.activeAiState(), QStringLiteral("idle"));
    QCOMPARE(controller.activeAiCompaction(), QVariantMap{});
    QVERIFY(controller.activeAiContextItems().isEmpty());
    QVERIFY(controller.activeAiToolApproval().isEmpty());
    QVERIFY(!controller.approveAiTool());
    QVERIFY(!controller.denyAiTool());
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("ai")));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchPage")).toString(),
             QStringLiteral("ai"));
    sessionState->selectedText = QStringLiteral("selected terminal evidence");
    QVERIFY(controller.attachAiSelection());
    QVERIFY(std::ranges::any_of(controller.activeAiContextItems(), [](const QVariant &item) {
        return item.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("attachment");
    }));
    QVERIFY(controller.activeAiContextPreview().contains(QStringLiteral("selected terminal evidence")));

    const QString attachmentPath = directory.filePath(QStringLiteral("agent-context.md"));
    QFile attachmentFile(attachmentPath);
    QVERIFY(attachmentFile.open(QIODevice::WriteOnly));
    QCOMPARE(attachmentFile.write("# Deployment note\nUse the staging cluster.\n"), qint64{43});
    attachmentFile.close();
    QVERIFY(controller.attachAiTextFiles(QStringList{QUrl::fromLocalFile(attachmentPath).toString()}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.activeAiContextPreview().contains(QStringLiteral("Deployment note")), 2'000);
    QVERIFY(std::ranges::any_of(controller.activeAiContextItems(), [](const QVariant &item) {
        const QVariantMap value = item.toMap();
        return value.value(QStringLiteral("kind")).toString() == QStringLiteral("attachment")
               && value.value(QStringLiteral("title")).toString() == QStringLiteral("agent-context.md")
               && value.value(QStringLiteral("preview")).toString().contains(QStringLiteral("Deployment note"));
    }));

    const QString imagePath = directory.filePath(QStringLiteral("terminal-screenshot.png"));
    QImage image(64, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#2457d6")));
    QVERIFY(image.save(imagePath, "PNG"));
    QVERIFY(controller.attachAiImageFiles(QStringList{QUrl::fromLocalFile(imagePath).toString()}));
    QTRY_VERIFY_WITH_TIMEOUT(
        std::ranges::any_of(controller.activeAiContextItems(),
                            [](const QVariant &item) {
                                const QVariantMap value = item.toMap();
                                return value.value(QStringLiteral("kind")).toString() == QStringLiteral("image")
                                       && value.value(QStringLiteral("title")).toString()
                                              == QStringLiteral("terminal-screenshot.png")
                                       && value.value(QStringLiteral("previewUrl"))
                                              .toString()
                                              .startsWith(QStringLiteral("data:image/png;base64,"));
                            }),
        2'000);

    sessionState->selectedText.clear();
    QVERIFY(controller.attachAiSelection());
    QVERIFY(!controller.activeAiError().isEmpty());
    QVERIFY(!controller.sendAiMessage(QStringLiteral("Explain the terminal")));
    QVERIFY(!controller.activeAiError().isEmpty());
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("ai")));
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("history")));
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("history")));
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
    QVERIFY(controller.attachAiRecentCommands(1));
    QVERIFY(controller.activeAiContextPreview().contains(QStringLiteral("Get-Date")));
    QVERIFY(controller.activeAiContextPreview().contains(QStringLiteral("Approximate terminal context")));
    QVERIFY(controller.activeAiContextPreview().contains(QStringLiteral("fake scrollback line")));
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
    QVERIFY(controller.toggleTerminalWorkbench(QStringLiteral("notes")));
    QCOMPARE(controller.terminalTabs().at(1).toMap().value(QStringLiteral("workbenchPage")).toString(),
             QStringLiteral("notes"));
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

void AppControllerTests::managesFreshTerminalTabWorkflows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });

    const QString first = controller.startLocalTerminal();
    const QString second = controller.startLocalTerminal();
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QCOMPARE(controller.terminalTabs().size(), 2);
    QVERIFY(controller.setTerminalTabTitle(second, QStringLiteral("Build shell")));
    QCOMPARE(controller.terminalTabs().at(1).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Build shell"));

    QVERIFY(controller.moveTerminalTab(second, 0));
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("id")).toString(), second);
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("canMoveLeft")).toBool(), false);
    QVERIFY(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("canMoveRight")).toBool());

    QVERIFY(controller.duplicateTerminalTab(second));
    QCOMPARE(controller.terminalTabs().size(), 3);
    const QString duplicate = controller.activeTerminalTabId();
    QVERIFY(duplicate != second);
    QCOMPARE(controller.terminalTabs().constLast().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Build shell"));

    QVERIFY(controller.closeTerminalTabsToRight(first));
    QCOMPARE(controller.terminalTabs().size(), 2);
    QVERIFY(controller.canReopenClosedTerminalTab());
    QVERIFY(controller.reopenLastClosedTerminalTab());
    QCOMPARE(controller.terminalTabs().size(), 3);
    QCOMPARE(controller.terminalTabs().constLast().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Build shell"));

    QVERIFY(controller.closeOtherTerminalTabs(second));
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("id")).toString(), second);
    QVERIFY(controller.closeTerminalTab(second));
    QVERIFY(controller.terminalTabs().isEmpty());
    QVERIFY(controller.reopenLastClosedTerminalTab());
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Build shell"));
    QCOMPARE(sessionState->starts, 5);
}

void AppControllerTests::managesPersistentTerminalWorkspaceSplits()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const auto firstState = std::make_shared<FakeLocalSessionState>();

    {
        ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, [firstState] {
            return std::make_unique<FakeLocalTerminalSession>(firstState);
        });
        const QString workspaceId = controller.startLocalTerminal();
        QVERIFY(!workspaceId.isEmpty());
        QCOMPARE(controller.terminalTabs().size(), 1);
        QCOMPARE(controller.activeTerminalWorkspace().value(QStringLiteral("paneCount")).toInt(), 1);

        QVERIFY(controller.splitActiveTerminal(QStringLiteral("horizontal"), false));
        QCOMPARE(controller.terminalTabs().size(), 1);
        QCOMPARE(firstState->starts, 2);
        QVariantMap workspace = controller.activeTerminalWorkspace();
        QCOMPARE(workspace.value(QStringLiteral("paneCount")).toInt(), 2);
        QVariantMap root = workspace.value(QStringLiteral("root")).toMap();
        QCOMPARE(root.value(QStringLiteral("kind")).toString(), QStringLiteral("split"));
        QCOMPARE(root.value(QStringLiteral("orientation")).toString(), QStringLiteral("horizontal"));
        const QString firstPaneId = root.value(QStringLiteral("first")).toMap().value(QStringLiteral("id")).toString();
        const QString secondPaneId =
            root.value(QStringLiteral("second")).toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(workspace.value(QStringLiteral("activePaneId")).toString(), secondPaneId);

        QVERIFY(controller.focusRelativeTerminalPane(-1));
        QCOMPARE(controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString(), firstPaneId);
        QVERIFY(controller.setTerminalSplitRatio(root.value(QStringLiteral("id")).toString(), 0.37));
        QCOMPARE(controller.activeTerminalWorkspace()
                     .value(QStringLiteral("root"))
                     .toMap()
                     .value(QStringLiteral("ratio"))
                     .toDouble(),
                 0.37);
        QVERIFY(controller.swapActiveTerminalPane(1));
        QVERIFY(controller.closeActiveTerminalPane());
        QCOMPARE(controller.activeTerminalWorkspace().value(QStringLiteral("paneCount")).toInt(), 1);
        QCOMPARE(firstState->stops, 1);

        QVERIFY(controller.splitActiveTerminal(QStringLiteral("vertical"), false));
        workspace = controller.activeTerminalWorkspace();
        root = workspace.value(QStringLiteral("root")).toMap();
        QCOMPARE(root.value(QStringLiteral("orientation")).toString(), QStringLiteral("vertical"));
        QVERIFY(controller.setTerminalSplitRatio(root.value(QStringLiteral("id")).toString(), 0.42));
        QCOMPARE(firstState->starts, 3);
    }
    QCOMPARE(firstState->stops, 3);

    const auto restoredState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController restored(profilesPath, knownHostsPath, settingsPath, [restoredState] {
        return std::make_unique<FakeLocalTerminalSession>(restoredState);
    });
    QCOMPARE(restored.terminalTabs().size(), 1);
    QCOMPARE(restoredState->starts, 2);
    const QVariantMap restoredWorkspace = restored.activeTerminalWorkspace();
    QCOMPARE(restoredWorkspace.value(QStringLiteral("paneCount")).toInt(), 2);
    const QVariantMap restoredRoot = restoredWorkspace.value(QStringLiteral("root")).toMap();
    QCOMPARE(restoredRoot.value(QStringLiteral("kind")).toString(), QStringLiteral("split"));
    QCOMPARE(restoredRoot.value(QStringLiteral("orientation")).toString(), QStringLiteral("vertical"));
    QCOMPARE(restoredRoot.value(QStringLiteral("ratio")).toDouble(), 0.42);
    QVERIFY(restored.closeTerminalTab(restored.activeTerminalTabId()));
    QVERIFY(restored.terminalTabs().isEmpty());
    QCOMPARE(restoredState->stops, 2);
}

void AppControllerTests::restoresSavedSshWorkspaceWithoutConnecting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    const QString credentialsPath = directory.filePath(QStringLiteral("credentials.json"));

    const std::array profiles{ztermy::ssh::SshProfile{
        .id = "saved-ssh",
        .name = "Saved SSH",
        .host = "192.0.2.44",
        .username = "operator",
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = "unused-test-key",
    }};
    QVERIFY(ztermy::ssh::SshProfileStore(profilesPath).save(profiles));

    ztermy::workbench::WorkspaceState persisted;
    persisted.terminalWorkspaces.push_back(ztermy::workbench::makeSinglePaneTerminalWorkspace(
        "ssh-workspace", "ssh-pane",
        ztermy::workbench::TerminalRestoreIntent{
            .id = "ssh-intent",
            .profileId = "saved-ssh",
            .title = "Saved SSH",
            .kind = ztermy::workbench::TerminalRestoreKind::SshProfile,
        }));
    persisted.activeTerminalWorkspaceId = "ssh-workspace";
    QVERIFY(ztermy::workbench::WorkspaceStateStore(directory.filePath(QStringLiteral("workspace_state.json")))
                .save(persisted));

    const auto localState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath, credentialsPath,
                                     ztermy::config::StorageMode::installed, [localState] {
                                         return std::make_unique<FakeLocalTerminalSession>(localState);
                                     });
    QCOMPARE(localState->starts, 0);
    QCOMPARE(controller.terminalTabs().size(), 1);
    QCOMPARE(controller.activeTerminalTabId(), QStringLiteral("ssh-workspace"));
    QCOMPARE(controller.activeTerminalWorkspace().value(QStringLiteral("paneCount")).toInt(), 1);
    QVariantMap tab = controller.terminalTabs().constFirst().toMap();
    QCOMPARE(tab.value(QStringLiteral("kind")).toString(), QStringLiteral("ssh"));
    QVERIFY(!tab.value(QStringLiteral("running")).toBool());
    QVERIFY(!tab.value(QStringLiteral("connecting")).toBool());
    QVERIFY(!tab.value(QStringLiteral("reconnecting")).toBool());
    QVERIFY(tab.value(QStringLiteral("canReconnect")).toBool());
    QVERIFY(tab.value(QStringLiteral("status")).toString().contains(QStringLiteral("reconnect"), Qt::CaseInsensitive));
    QVERIFY(!controller.hostKeyPromptVisible());

    QSignalSpy workspaceChanged(&controller, &ztermy::AppController::terminalWorkspaceChanged);
    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QVERIFY(workspaceChanged.count() >= 1);
    QCOMPARE(localState->starts, 1);
}

void AppControllerTests::managesSessionAppearanceAndStructuredRecording()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sessionState = std::make_shared<FakeLocalSessionState>();
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")), [sessionState] {
                                         return std::make_unique<FakeLocalTerminalSession>(sessionState);
                                     });

    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QVERIFY(!controller.setActiveTerminalEncoding(QStringLiteral("gb18030")));
    QVERIFY(controller.setActiveTerminalAppearance(QStringLiteral("Cascadia Mono"), 16, false, 0.55,
                                                   QStringLiteral("bar"), QStringLiteral("#FFEEDD"),
                                                   QStringLiteral("#112233")));

    QVariantMap tab = controller.terminalTabs().constFirst().toMap();
    QCOMPARE(tab.value(QStringLiteral("sessionFontFamily")).toString(), QStringLiteral("Cascadia Mono"));
    QCOMPARE(tab.value(QStringLiteral("sessionFontSize")).toInt(), 16);
    QVERIFY(!tab.value(QStringLiteral("sessionLigatures")).toBool());
    QCOMPARE(tab.value(QStringLiteral("sessionBackgroundOpacity")).toDouble(), 0.55);
    QCOMPARE(tab.value(QStringLiteral("sessionCursor")).toString(), QStringLiteral("bar"));
    QCOMPARE(tab.value(QStringLiteral("sessionForeground")).toString(), QStringLiteral("#ffeedd"));
    QCOMPARE(tab.value(QStringLiteral("sessionBackground")).toString(), QStringLiteral("#112233"));
    QVERIFY(!controller.setActiveTerminalAppearance(QString(), 16, false, 0.55, QStringLiteral("bar"),
                                                    QStringLiteral("#FFEEDD"), QStringLiteral("#112233")));
    QVERIFY(!controller.setActiveTerminalAppearance(QStringLiteral("Cascadia Mono"), 16, false, 1.1,
                                                    QStringLiteral("bar"), QStringLiteral("#FFEEDD"),
                                                    QStringLiteral("#112233")));
    QVERIFY(controller.resetActiveTerminalAppearance());
    tab = controller.terminalTabs().constFirst().toMap();
    QVERIFY(tab.value(QStringLiteral("sessionFontFamily")).toString().isEmpty());
    QCOMPARE(tab.value(QStringLiteral("sessionFontSize")).toInt(), 0);
    QCOMPARE(tab.value(QStringLiteral("sessionBackgroundOpacity")).toDouble(), -1.0);

    QVERIFY(controller.startTerminalScriptRecording());
    QVERIFY(controller.runTerminalCommand(QStringLiteral("Get-Process")));
    QVERIFY(controller.pauseTerminalScriptRecording());
    QVERIFY(controller.runTerminalCommand(QStringLiteral("not-recorded-while-paused")));
    QVERIFY(controller.resumeTerminalScriptRecording());
    QVERIFY(controller.runTerminalCommand(QStringLiteral("Get-Date")));
    QVERIFY(controller.stopTerminalScriptRecording());

    tab = controller.terminalTabs().constFirst().toMap();
    QCOMPARE(tab.value(QStringLiteral("scriptRecordingState")).toString(), QStringLiteral("review"));
    const QVariantList steps = tab.value(QStringLiteral("scriptRecordingSteps")).toList();
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps.at(0).toMap().value(QStringLiteral("command")).toString(), QStringLiteral("Get-Process"));
    QCOMPARE(steps.at(1).toMap().value(QStringLiteral("command")).toString(), QStringLiteral("Get-Date"));
    QCOMPARE(sessionState->inputs.size(), 3);

    QVERIFY(controller.saveTerminalScriptRecordingAsScript(QStringLiteral("Recorded maintenance"),
                                                           QStringLiteral("Captured from the composer")));
    QCOMPARE(controller.quickCommands().size(), 1);
    const QVariantMap recordedScript = controller.quickCommands().constFirst().toMap();
    QCOMPARE(recordedScript.value(QStringLiteral("name")).toString(), QStringLiteral("Recorded maintenance"));
    const QVariantList recordedSteps = recordedScript.value(QStringLiteral("steps")).toList();
    QCOMPARE(recordedSteps.size(), 2);
    QCOMPARE(recordedSteps.at(0).toMap().value(QStringLiteral("command")).toString(), QStringLiteral("Get-Process"));
    QCOMPARE(recordedSteps.at(1).toMap().value(QStringLiteral("command")).toString(), QStringLiteral("Get-Date"));

    controller.clearTerminalScriptRecording();
    tab = controller.terminalTabs().constFirst().toMap();
    QCOMPARE(tab.value(QStringLiteral("scriptRecordingState")).toString(), QStringLiteral("idle"));
    QVERIFY(tab.value(QStringLiteral("scriptRecordingSteps")).toList().isEmpty());
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

void AppControllerTests::exposesAndDismissesStartupRecoveryNotice()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    ztermy::ssh::SshProfile first{
        .id = "recoverable-profile",
        .name = "Last known good",
        .host = "192.0.2.44",
        .username = "operator",
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = "unused-test-key",
    };
    auto second = first;
    second.name = "Damaged generation";
    const std::array firstGeneration{first};
    const std::array secondGeneration{second};
    const ztermy::ssh::SshProfileStore store(profilesPath);
    QVERIFY(store.save(firstGeneration));
    QVERIFY(store.save(secondGeneration));

    QFile primary(profilesPath);
    QVERIFY(primary.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(primary.write("{truncated"), qint64{10});
    primary.close();

    ztermy::AppController controller(profilesPath);
    QCOMPARE(controller.hostProfiles().size(), 1);
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Last known good"));
    QVERIFY(!controller.startupRecoveryNotice().isEmpty());
    QSignalSpy noticeChanged(&controller, &ztermy::AppController::startupRecoveryNoticeChanged);
    controller.dismissStartupRecoveryNotice();
    QVERIFY(controller.startupRecoveryNotice().isEmpty());
    QCOMPARE(noticeChanged.count(), 1);
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

void AppControllerTests::persistsAiQuickMessages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));

    QString firstId;
    {
        ztermy::AppController controller(profilesPath);
        QSignalSpy changes(&controller, &ztermy::AppController::aiQuickMessagesChanged);
        QVERIFY(controller.aiQuickMessages().isEmpty());
        QVERIFY(controller.saveAiQuickMessage({}, QStringLiteral("Service status"), QStringLiteral(" Service Status "),
                                              QStringLiteral("Inspect failed services and summarize causes."),
                                              QStringLiteral("Diagnose service failures")));
        QCOMPARE(controller.aiQuickMessages().size(), 1);
        const QVariantMap first = controller.aiQuickMessages().constFirst().toMap();
        QCOMPARE(first.value(QStringLiteral("slug")).toString(), QStringLiteral("service-status"));
        firstId = first.value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());
        QVERIFY(!controller.saveAiQuickMessage({}, QStringLiteral("Reserved"), QStringLiteral("new"),
                                               QStringLiteral("Reserved prompt"), {}));
        QVERIFY(!controller.aiQuickMessageError().isEmpty());
        QVERIFY(!controller.saveAiQuickMessage({}, QStringLiteral("Duplicate"), QStringLiteral("service-status"),
                                               QStringLiteral("Duplicate prompt"), {}));
        QVERIFY(controller.saveAiQuickMessage(firstId, QStringLiteral("Service health"),
                                              QStringLiteral("service-health"),
                                              QStringLiteral("Inspect service health.\r\nExplain failures."), {}));
        QCOMPARE(controller.aiQuickMessages().constFirst().toMap().value(QStringLiteral("content")).toString(),
                 QStringLiteral("Inspect service health.\nExplain failures."));
        QVERIFY(changes.count() >= 3);
    }

    ztermy::AppController reloaded(profilesPath);
    QCOMPARE(reloaded.aiQuickMessages().size(), 1);
    QCOMPARE(reloaded.aiQuickMessages().constFirst().toMap().value(QStringLiteral("slug")).toString(),
             QStringLiteral("service-health"));
    QVERIFY(reloaded.deleteAiQuickMessage(firstId));
    QVERIFY(reloaded.aiQuickMessages().isEmpty());
}

void AppControllerTests::scansAndExposesAiUserSkills()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString skillDirectory = directory.filePath(QStringLiteral("Skills/service-diagnostics"));
    QVERIFY(QDir().mkpath(skillDirectory));
    QFile skill(QDir(skillDirectory).filePath(QStringLiteral("SKILL.md")));
    QVERIFY(skill.open(QIODevice::WriteOnly));
    const QByteArray contents = QByteArrayLiteral(
        "---\nname: service-diagnostics\ndescription: Diagnose service failures.\n---\nInspect status and logs.\n");
    QCOMPARE(skill.write(contents), contents.size());
    skill.close();

    ztermy::AppController controller(profilesPath);
    QCOMPARE(controller.aiUserSkillsState(), QStringLiteral("idle"));
    QCOMPARE(controller.aiUserSkillsPath(), QDir::toNativeSeparators(directory.filePath(QStringLiteral("Skills"))));
    QSignalSpy changes(&controller, &ztermy::AppController::aiUserSkillsChanged);
    controller.ensureAiUserSkillsLoaded();
    QTRY_COMPARE_WITH_TIMEOUT(controller.aiUserSkillsState(), QStringLiteral("ready"), 5000);
    QVERIFY(changes.count() >= 2);
    QCOMPARE(controller.aiUserSkills().size(), 1);
    const QVariantMap exposed = controller.aiUserSkills().constFirst().toMap();
    QCOMPARE(exposed.value(QStringLiteral("id")).toString(), QStringLiteral("service-diagnostics"));
    QVERIFY(exposed.value(QStringLiteral("ready")).toBool());
    QVERIFY(exposed.value(QStringLiteral("warnings")).toStringList().isEmpty());
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

void AppControllerTests::rendersAndRunsScriptAgainstFixedTerminal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QList<std::shared_ptr<FakeLocalSessionState>> sessions;
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")),
                                     directory.filePath(QStringLiteral("known_hosts.json")),
                                     directory.filePath(QStringLiteral("settings.json")), [&sessions] {
                                         auto state = std::make_shared<FakeLocalSessionState>();
                                         sessions.append(state);
                                         return std::make_unique<FakeLocalTerminalSession>(std::move(state));
                                     });

    QVERIFY(!controller.startLocalTerminal().isEmpty());
    const QString firstSessionId =
        controller.terminalTabs().constFirst().toMap().value(QStringLiteral("sessionId")).toString();
    QVERIFY(!controller.startLocalTerminal().isEmpty());
    QCOMPARE(sessions.size(), 2);

    const QVariantMap script{
        {QStringLiteral("name"), QStringLiteral("Readiness gate")},
        {QStringLiteral("description"), QStringLiteral("Waits for output before continuing")},
        {QStringLiteral("shell"), QStringLiteral("any")},
        {QStringLiteral("variables"), QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("target")},
                                                               {QStringLiteral("label"), QStringLiteral("Target")},
                                                               {QStringLiteral("type"), QStringLiteral("text")},
                                                               {QStringLiteral("defaultValue"), QString{}},
                                                               {QStringLiteral("choices"), QVariantList{}},
                                                               {QStringLiteral("required"), true}}}},
        {QStringLiteral("steps"),
         QVariantList{
             QVariantMap{{QStringLiteral("command"), QStringLiteral("echo ${target}")},
                         {QStringLiteral("continuation"), QStringLiteral("literal-output")},
                         {QStringLiteral("outputMarker"), QStringLiteral("READY:${target}")},
                         {QStringLiteral("timeoutMs"), 5000}},
             QVariantMap{{QStringLiteral("command"), QStringLiteral("echo done")},
                         {QStringLiteral("continuation"), QStringLiteral("immediate")},
                         {QStringLiteral("outputMarker"), QString{}},
                         {QStringLiteral("timeoutMs"), 30000}},
         }},
    };
    QVERIFY(controller.saveScript(script));
    const QVariantMap saved = controller.quickCommands().constFirst().toMap();
    const QString scriptId = saved.value(QStringLiteral("id")).toString();
    QVERIFY(!scriptId.isEmpty());
    QVERIFY(!controller.renderScript(scriptId, {}).value(QStringLiteral("ok")).toBool());
    const QVariantMap variables{{QStringLiteral("target"), QStringLiteral("ztermy")}};
    const QVariantMap preview = controller.renderScript(scriptId, variables);
    QVERIFY(preview.value(QStringLiteral("ok")).toBool());
    QCOMPARE(preview.value(QStringLiteral("steps")).toList().size(), 2);
    QVERIFY(!controller.runScript(scriptId, variables, QStringLiteral("missing-session")));

    QVERIFY(controller.runScript(scriptId, variables, firstSessionId));
    QCOMPARE(sessions.at(0)->inputs, QList<QByteArray>{QByteArray("echo ztermy\r")});
    QVERIFY(sessions.at(1)->inputs.isEmpty());
    const auto firstTab = [&controller, &firstSessionId] {
        const QVariantList tabs = controller.terminalTabs();
        const auto found = std::ranges::find(tabs, firstSessionId, [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("sessionId")).toString();
        });
        return found == tabs.end() ? QVariantMap{} : found->toMap();
    };
    QCOMPARE(firstTab().value(QStringLiteral("scriptExecutionState")).toString(), QStringLiteral("waiting-output"));
    QVERIFY(!controller.runScript(scriptId, variables, firstSessionId));
    QVERIFY(sessions.at(0)->outputSink != nullptr);
    const QByteArray prefix("REA");
    sessions.at(0)->outputSink->append(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(prefix.constData()), static_cast<std::size_t>(prefix.size())));
    const QByteArray suffix("DY:ztermy");
    sessions.at(0)->outputSink->append(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(suffix.constData()), static_cast<std::size_t>(suffix.size())));
    QTRY_COMPARE(sessions.at(0)->inputs.size(), 2);
    QCOMPARE(sessions.at(0)->inputs.constLast(), QByteArray("echo done\r"));
    QCOMPARE(firstTab().value(QStringLiteral("scriptExecutionState")).toString(), QStringLiteral("completed"));

    QVERIFY(controller.runScript(scriptId, variables, firstSessionId));
    QVERIFY(controller.cancelScript(firstSessionId));
    QCOMPARE(firstTab().value(QStringLiteral("scriptExecutionState")).toString(), QStringLiteral("cancelled"));
    QVERIFY(!controller.cancelScript(firstSessionId));
}

void AppControllerTests::managesLocalMarkdownNotesAndLatestSearch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilesPath = directory.filePath(QStringLiteral("profiles.json"));
    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const QString settingsPath = directory.filePath(QStringLiteral("settings.json"));
    ztermy::AppController controller(profilesPath, knownHostsPath, settingsPath);

    QVERIFY(controller.notes().isEmpty());
    QVERIFY(controller.createNoteFolder(QStringLiteral("手册")));
    QVERIFY(controller.createNote(QStringLiteral("手册/first.md")));
    QCOMPARE(controller.activeNotePath(), QStringLiteral("手册/first.md"));
    controller.updateActiveNoteContent(QStringLiteral("# First\n\nfirst marker"));
    QVERIFY(controller.activeNoteDirty());
    QVERIFY(!controller.createNote(QStringLiteral("手册/blocked.md")));
    QVERIFY(!controller.openNote(QStringLiteral("missing.md")));
    QVERIFY(controller.saveActiveNote());
    QVERIFY(!controller.activeNoteDirty());

    QVERIFY(controller.createNote(QStringLiteral("手册/second.md")));
    controller.updateActiveNoteContent(QStringLiteral("# Second\n\nsecond marker"));
    QVERIFY(controller.saveActiveNote());
    QVERIFY(controller.openNote(QStringLiteral("手册/first.md")));
    controller.updateActiveNoteContent(QStringLiteral("unsaved draft"));
    QVERIFY(!controller.openNote(QStringLiteral("手册/second.md")));
    QVERIFY(controller.discardActiveNoteChanges());
    QCOMPARE(controller.activeNoteContent(), QStringLiteral("# First\n\nfirst marker"));

    controller.searchNotes(QStringLiteral("second marker"));
    controller.searchNotes(QStringLiteral("first marker"));
    QTRY_COMPARE(controller.noteSearchState(), QStringLiteral("ready"));
    QCOMPARE(controller.noteSearchResults().size(), 1);
    QCOMPARE(controller.noteSearchResults().constFirst().toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("手册/first.md"));

    QVERIFY(controller.renameNoteEntry(QStringLiteral("手册/first.md"), QStringLiteral("手册/renamed.md")));
    QCOMPARE(controller.activeNotePath(), QStringLiteral("手册/renamed.md"));
    const QString exportPath = directory.filePath(QStringLiteral("exported.md"));
    QVERIFY(controller.exportActiveNote(QUrl::fromLocalFile(exportPath).toString()));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(exported.readAll().contains("first marker"));

    ztermy::AppController reloaded(profilesPath, knownHostsPath, settingsPath);
    QCOMPARE(reloaded.notes().size(), 3);
    QVERIFY(reloaded.openNote(QStringLiteral("手册/renamed.md")));
    QCOMPARE(reloaded.activeNoteContent(), QStringLiteral("# First\n\nfirst marker"));
    QVERIFY(reloaded.deleteNoteEntry(QStringLiteral("手册")));
    QVERIFY(reloaded.notes().isEmpty());
}

void AppControllerTests::reconnectsSavedKeyProfileOnRealHost()
{
    if (qEnvironmentVariable("ZTERMY_TEST_SSH_RECONNECT_INTERACTIVE") != QStringLiteral("1"))
    {
        QSKIP("Set ZTERMY_TEST_SSH_RECONNECT_INTERACTIVE=1 to run the real-host reconnect gate.");
    }
    const QString host = qEnvironmentVariable("ZTERMY_TEST_SSH_HOST");
    const QString username = qEnvironmentVariable("ZTERMY_TEST_SSH_USERNAME");
    const QString keyPath = qEnvironmentVariable("ZTERMY_TEST_SSH_KEY_PATH");
    const QString expectedFingerprint = qEnvironmentVariable("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    QVERIFY(!host.isEmpty());
    QVERIFY(!username.isEmpty());
    QVERIFY(!keyPath.isEmpty());
    QVERIFY(!expectedFingerprint.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));
    const QVariantMap options{
        {QStringLiteral("terminalType"), QStringLiteral("xterm-256color")},
        {QStringLiteral("keepaliveIntervalSeconds"), 0},
        {QStringLiteral("keepaliveFailureThreshold"), 3},
        {QStringLiteral("startupCommand"), QStringLiteral("exit")},
        {QStringLiteral("startupCommandMode"), QStringLiteral("paste")},
        {QStringLiteral("startupLineDelayMilliseconds"), 100},
        {QStringLiteral("environment"), QVariantList{}},
        {QStringLiteral("reconnectPolicy"), QStringLiteral("transport-failure")},
        {QStringLiteral("reconnectMaximumAttempts"), 2},
        {QStringLiteral("reconnectInitialBackoffMilliseconds"), 250},
    };
    QVERIFY(controller.saveHostProfileWithCredential({}, QStringLiteral("Reconnect gate"), host, 22, username,
                                                     QStringLiteral("private-key"), keyPath, false, {}, {}, false,
                                                     options));
    const QString profileId = controller.hostProfiles().constFirst().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(controller.connectHostProfile(profileId, {}));

    bool fingerprintMismatch = false;
    const auto reconnectBudgetExhausted = [&] {
        if (controller.hostKeyPromptVisible())
        {
            if (controller.hostKeyFingerprint() != expectedFingerprint)
            {
                fingerprintMismatch = true;
                controller.rejectHostKey();
                return true;
            }
            controller.acceptHostKey(false);
        }
        const QVariantList tabs = controller.terminalTabs();
        if (tabs.isEmpty())
        {
            return false;
        }
        const QVariantMap tab = tabs.constFirst().toMap();
        return tab.value(QStringLiteral("reconnectAttempt")).toInt() == 2
               && !tab.value(QStringLiteral("reconnecting")).toBool()
               && (tab.value(QStringLiteral("failed")).toBool() || tab.value(QStringLiteral("remoteClosed")).toBool());
    };
    QTRY_VERIFY_WITH_TIMEOUT(reconnectBudgetExhausted(), 20'000);
    QVERIFY(!fingerprintMismatch);
}

QTEST_GUILESS_MAIN(AppControllerTests)

#include "app_controller_tests.moc"
