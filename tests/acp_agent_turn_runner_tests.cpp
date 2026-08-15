#include "application/ai/AcpAgentTurnRunner.h"
#include "infrastructure/ai/AcpAgentDiscovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtTest/QTest>

#include <optional>

namespace
{

[[nodiscard]] QString testAgentPath()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_acp_test_agent.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_acp_test_agent");
#endif
}

[[nodiscard]] ztermy::ai::AcpClientConfiguration configuration(const QStringList &arguments)
{
    return {.program = testAgentPath(),
            .arguments = arguments,
            .processWorkingDirectory = QDir::tempPath(),
            .sessionWorkingDirectory = QStringLiteral("/srv/project"),
            .clientVersion = "0.3.0-test"};
}

[[nodiscard]] ztermy::ai::AiToolOutput accepted(const ztermy::ai::AiToolCall &call)
{
    return {.callId = call.id,
            .name = call.name,
            .outputJson = R"({"ok":true,"status":"accepted","command_id":"command-1"})"};
}

class AcpAgentTurnRunnerTests final : public QObject
{
    Q_OBJECT

private slots:
    void discoversInstalledAgentVersion();
    void bridgesCompleteCurrentTerminalLifecycle();
    void resumesAnApprovedTerminalCreate();
    void surfacesAndCompletesAgentPermission();
};

void AcpAgentTurnRunnerTests::discoversInstalledAgentVersion()
{
    ztermy::ai::AcpAgentDiscovery discovery;
    std::optional<std::expected<ztermy::ai::AcpAgentInstallation, QString>> result;
    const auto started = discovery.start(testAgentPath(), [&result](auto discovered) {
        result.emplace(std::move(discovered));
    });
    if (!started.has_value())
    {
        QFAIL(qPrintable(started.error()));
    }
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5'000);
    const auto discovered = result.value_or(std::unexpected(QStringLiteral("No discovery result.")));
    if (!discovered.has_value())
    {
        QFAIL(qPrintable(discovered.error()));
    }
    const auto installation = discovered.value_or(ztermy::ai::AcpAgentInstallation{});
    QCOMPARE(installation.program, testAgentPath());
    QCOMPARE(installation.version, QStringLiteral("opencode 1.18.5-test"));
}

void AcpAgentTurnRunnerTests::bridgesCompleteCurrentTerminalLifecycle()
{
    ztermy::ai::AcpAgentTurnRunner runner;
    QEventLoop loop;
    QString failure;
    QString encodedCommand;
    int observations = 0;
    int activities = 0;
    bool finished = false;
    bool sawText = false;
    bool sawReasoning = false;

    const auto started = runner.startConfigured(
        configuration({QStringLiteral("--full-terminal")}), "inspect disk", ztermy::ai::AcpTerminalShell::posix,
        [&failure, &sawText, &sawReasoning](const auto, const ztermy::ai::AiStreamEvent &event) {
            if (event.type == ztermy::ai::AiStreamEventType::textDelta)
            {
                sawText = event.delta == "Done";
            }
            else if (event.type == ztermy::ai::AiStreamEventType::reasoningDelta)
            {
                sawReasoning = event.delta == "Inspecting";
            }
            else if (event.type == ztermy::ai::AiStreamEventType::responseFailed && event.error.has_value())
            {
                failure = QString::fromUtf8(event.error->message);
            }
        },
        [&loop, &finished](const auto, const auto &) {
            finished = true;
            loop.quit();
        },
        [&encodedCommand](const ztermy::ai::AiToolCall &call)
            -> std::expected<ztermy::ai::AiTurnRunner::ToolHandlingResult, ztermy::ai::AiProviderError> {
            const QJsonObject arguments =
                QJsonDocument::fromJson(QByteArray::fromStdString(call.argumentsJson)).object();
            encodedCommand = arguments.value(QStringLiteral("command")).toString();
            return ztermy::ai::AiTurnRunner::ToolHandlingResult{.output = accepted(call), .sideEffecting = true};
        },
        {},
        [&observations](const std::string_view commandId)
            -> std::expected<ztermy::ai::AcpTerminalSnapshot, ztermy::ai::AiProviderError> {
            if (commandId != "command-1")
            {
                return std::unexpected(ztermy::ai::AiProviderError{.code = ztermy::ai::AiProviderErrorCode::protocol,
                                                                   .message = "wrong command",
                                                                   .retryable = false});
            }
            ++observations;
            return ztermy::ai::AcpTerminalSnapshot{.output = "0123456789",
                                                   .exitCode = observations > 1 ? std::optional<int>{0} : std::nullopt,
                                                   .exited = observations > 1};
        },
        {}, {},
        [&activities](const ztermy::ai::AiToolActivity &) {
            ++activities;
        });
    QVERIFY2(started.has_value(), started.has_value() ? "" : started.error().message.c_str());
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(finished);
    QVERIFY(sawText);
    QVERIFY(sawReasoning);
    QCOMPARE(activities, 2);
    QVERIFY(observations >= 3);
    QVERIFY(encodedCommand.startsWith(QStringLiteral("(cd -- '/srv/project' && env 'ZTERMY_TEST=yes'")));
    QVERIFY(encodedCommand.contains(QStringLiteral("'printf' 'hello world'")));
    QVERIFY(!runner.active());
}

void AcpAgentTurnRunnerTests::resumesAnApprovedTerminalCreate()
{
    ztermy::ai::AcpAgentTurnRunner runner;
    QEventLoop loop;
    std::optional<ztermy::ai::AiToolCall> pending;
    bool finished = false;

    const auto started = runner.startConfigured(
        configuration({}), "run", ztermy::ai::AcpTerminalShell::posix, {},
        [&loop, &finished](const auto, const auto &) {
            finished = true;
            loop.quit();
        },
        [&pending](const ztermy::ai::AiToolCall &call)
            -> std::expected<ztermy::ai::AiTurnRunner::ToolHandlingResult, ztermy::ai::AiProviderError> {
            pending = call;
            return ztermy::ai::AiTurnRunner::ToolHandlingResult{.cancel =
                                                                    [] {
                                                                    },
                                                                .sideEffecting = true};
        },
        {},
        [](const std::string_view) -> std::expected<ztermy::ai::AcpTerminalSnapshot, ztermy::ai::AiProviderError> {
            return ztermy::ai::AcpTerminalSnapshot{.exitCode = 0, .exited = true};
        });
    QVERIFY(started.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(pending.has_value(), 3000);
    QCOMPARE(runner.pendingToolCall(), pending);
    QVERIFY(runner.completePendingTool(accepted(*pending)));
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(finished);
}

void AcpAgentTurnRunnerTests::surfacesAndCompletesAgentPermission()
{
    ztermy::ai::AcpAgentTurnRunner runner;
    QEventLoop loop;
    bool permissionPending = false;
    bool finished = false;

    const auto started = runner.startConfigured(
        configuration({QStringLiteral("--permission")}), "run", ztermy::ai::AcpTerminalShell::posix, {},
        [&loop, &finished](const auto, const auto &) {
            finished = true;
            loop.quit();
        },
        [](const ztermy::ai::AiToolCall &call)
            -> std::expected<ztermy::ai::AiTurnRunner::ToolHandlingResult, ztermy::ai::AiProviderError> {
            return ztermy::ai::AiTurnRunner::ToolHandlingResult{.output = accepted(call), .sideEffecting = true};
        },
        {},
        [](const std::string_view) -> std::expected<ztermy::ai::AcpTerminalSnapshot, ztermy::ai::AiProviderError> {
            return ztermy::ai::AcpTerminalSnapshot{.exitCode = 0, .exited = true};
        },
        [](const ztermy::ai::AcpPermissionRequest &)
            -> std::expected<std::optional<QString>, ztermy::ai::AiProviderError> {
            return std::optional<QString>{};
        },
        [&permissionPending](const ztermy::ai::AcpPermissionRequest &) {
            permissionPending = true;
        });
    QVERIFY(started.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(permissionPending, 3000);
    const auto request = runner.pendingPermission();
    QVERIFY(request.has_value());
    const auto permissionRequest = request.value_or(ztermy::ai::AcpPermissionRequest{});
    QCOMPARE(permissionRequest.toolCallId, QStringLiteral("tool-1"));
    QCOMPARE(permissionRequest.options.size(), std::size_t{2});
    QVERIFY(runner.completePendingPermission(QStringLiteral("allow-once")));
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(finished);
}

} // namespace

QTEST_GUILESS_MAIN(AcpAgentTurnRunnerTests)

#include "acp_agent_turn_runner_tests.moc"
