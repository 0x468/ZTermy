#include "application/ai/AiActionToolDispatcher.h"
#include "application/ai/AiWaitCommandTool.h"
#include "application/terminal/LocalTerminalSession.h"
#include "domain/ai/AiCommandTracker.h"
#include "domain/terminal/SemanticTerminalObserver.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QtTest/QTest>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{

using ztermy::ai::AiActionToolContext;
using ztermy::ai::AiActionToolDispatcher;
using ztermy::ai::AiActionToolDisposition;
using ztermy::ai::AiAgentTurnBudget;
using ztermy::ai::AiCommandTracker;
using ztermy::ai::AiPermissionMode;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolDispatchState;
using ztermy::ai::AiTrackedCommand;
using ztermy::ai::AiTrackedCommandState;
using ztermy::ai::AiWaitCommandTool;
using ztermy::terminal::CommandBlock;
using ztermy::terminal::CommandBlockId;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::SemanticTerminalObserver;

constexpr std::string_view sessionId = "local-agent-scenario";
constexpr std::uint64_t sessionGeneration = 0;

class SemanticOutputSink final : public ztermy::terminal::TerminalOutputSink
{
public:
    explicit SemanticOutputSink(std::shared_ptr<SemanticTerminalObserver> observer) : m_observer(std::move(observer)) {}

    void append(const std::span<const std::byte> bytes) noexcept override { m_observer->append(bytes); }

private:
    std::shared_ptr<SemanticTerminalObserver> m_observer;
};

[[nodiscard]] bool waitUntil(const std::function<bool()> &predicate, const int timeoutMilliseconds = 8'000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds)
    {
        if (predicate())
        {
            return true;
        }
        QTest::qWait(10);
    }
    return predicate();
}

[[nodiscard]] std::optional<CommandBlock> blockFor(const std::shared_ptr<SemanticTerminalObserver> &observer,
                                                   const std::string_view command,
                                                   const CommandBlockState state = CommandBlockState::finished)
{
    const auto snapshot = observer->snapshot();
    const auto position = std::ranges::find_if(snapshot.commandBlocks, [command, state](const CommandBlock &block) {
        return block.command == command && block.state == state;
    });
    return position == snapshot.commandBlocks.end() ? std::nullopt : std::optional<CommandBlock>{*position};
}

[[nodiscard]] CommandBlockId latestBlockId(const std::shared_ptr<SemanticTerminalObserver> &observer)
{
    const auto snapshot = observer->snapshot();
    return snapshot.commandBlocks.empty() ? 0 : snapshot.commandBlocks.back().id;
}

[[nodiscard]] AiActionToolContext localContext(const std::string &conversation, const AiPermissionMode mode)
{
    return {.conversationId = conversation,
            .turnId = 1,
            .target = {.sessionId = std::string(sessionId), .sessionGeneration = sessionGeneration},
            .permissionMode = mode};
}

[[nodiscard]] AiToolCall commandCall(const std::string &id, const std::string &command)
{
    QJsonObject arguments{{QStringLiteral("session_id"), QString::fromLatin1(sessionId)},
                          {QStringLiteral("session_generation"), static_cast<qint64>(sessionGeneration)},
                          {QStringLiteral("command"), QString::fromUtf8(command)}};
    return {.id = id,
            .name = "run_command",
            .argumentsJson = QJsonDocument(arguments).toJson(QJsonDocument::Compact).toStdString()};
}

[[nodiscard]] AiToolCall ptyCall(const std::string &id, const std::string &data)
{
    QJsonObject arguments{{QStringLiteral("session_id"), QString::fromLatin1(sessionId)},
                          {QStringLiteral("session_generation"), static_cast<qint64>(sessionGeneration)},
                          {QStringLiteral("data"), QString::fromUtf8(data)},
                          {QStringLiteral("append_enter"), true}};
    return {.id = id,
            .name = "write_to_pty",
            .argumentsJson = QJsonDocument(arguments).toJson(QJsonDocument::Compact).toStdString()};
}

[[nodiscard]] QJsonObject jsonObject(const std::string &json)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

class AiAgentScenarioTests final : public QObject
{
    Q_OBJECT

private slots:
    void executesLocalAgentLifecycleAcrossModes();
    void appliesSshProfileMutationModeMatrix();
};

void AiAgentScenarioTests::executesLocalAgentLifecycleAcrossModes()
{
    constexpr std::string_view nonce = "87654321-abcd-4321-abcd-1234567890ab";
    auto observer = std::make_shared<SemanticTerminalObserver>(
        ztermy::terminal::CommandBlockSessionContext{.sessionId = std::string(sessionId),
                                                     .host = "localhost",
                                                     .shell = "pwsh"},
        std::string(nonce));
    auto sink = std::make_shared<SemanticOutputSink>(observer);
    ztermy::terminal::LocalTerminalSession session;
    session.setOutputSink(sink);
    session.setShellIntegrationNonce(std::string(nonce));

    const std::error_code startError = session.start({.columns = 100, .rows = 30});
    if (startError)
    {
        QFAIL(startError.message().c_str());
    }

    const auto stopSession = qScopeGuard([&session] {
        session.stop();
    });

    struct ModeCase final
    {
        AiPermissionMode mode;
        AiActionToolDisposition disposition;
        std::string_view label;
    };
    constexpr std::array modes{
        ModeCase{.mode = AiPermissionMode::readOnly,
                 .disposition = AiActionToolDisposition::respond,
                 .label = "READ_ONLY"},
        ModeCase{.mode = AiPermissionMode::ask, .disposition = AiActionToolDisposition::awaitApproval, .label = "ASK"},
        ModeCase{.mode = AiPermissionMode::automatic, .disposition = AiActionToolDisposition::execute, .label = "AUTO"},
        ModeCase{.mode = AiPermissionMode::yolo, .disposition = AiActionToolDisposition::execute, .label = "YOLO"},
    };

    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        const auto &mode = modes[index];
        AiActionToolDispatcher dispatcher;
        AiAgentTurnBudget budget;
        const std::string command = "Write-Output 'ZTERMY_AGENT_" + std::string(mode.label) + "'";
        const std::string callId = "mode-" + std::to_string(index);
        const auto call = commandCall(callId, command);
        const auto plan =
            dispatcher.prepare(call, localContext("mode-conversation-" + std::to_string(index), mode.mode), budget);
        QCOMPARE(plan.disposition, mode.disposition);
        if (mode.mode == AiPermissionMode::readOnly)
        {
            QCOMPARE(jsonObject(plan.outputJson)
                         .value(QStringLiteral("error"))
                         .toObject()
                         .value(QStringLiteral("code"))
                         .toString(),
                     QStringLiteral("permission_denied"));
            QCOMPARE(budget.writeActions(), std::uint32_t{0});
            continue;
        }

        QVERIFY(plan.action.has_value());
        const auto action = *plan.action;
        if (plan.disposition == AiActionToolDisposition::awaitApproval)
        {
            QVERIFY(dispatcher.approve(action));
        }

        AiCommandTracker tracker;
        const std::string commandId = "tracked-" + std::to_string(index);
        QVERIFY(tracker.accept({.id = commandId,
                                .conversationId = "mode-conversation-" + std::to_string(index),
                                .target = action.target,
                                .command = action.command,
                                .baselineBlockId = latestBlockId(observer)}));
        session.queueInput(QByteArray::fromStdString(action.command + '\r'));
        QVERIFY2(waitUntil([&observer, &command] {
                     return blockFor(observer, command).has_value();
                 }),
                 "The local Agent command did not reach a finished semantic block");

        const auto semantic = observer->snapshot();
        const auto tracked = tracker.observe(commandId, semantic.commandBlocks, true);
        QVERIFY(tracked.has_value());
        const auto trackedValue = tracked.value_or(AiTrackedCommand{});
        QCOMPARE(trackedValue.state, AiTrackedCommandState::finished);
        QCOMPARE(trackedValue.exitStatus, std::optional<int>{0});
        const auto result = AiWaitCommandTool::result(trackedValue);
        const auto serialized = jsonObject(result).value(QStringLiteral("command")).toObject();
        QVERIFY(serialized.value(QStringLiteral("output_complete")).toBool());
        QVERIFY(serialized.value(QStringLiteral("output")).toString().contains(QString::fromLatin1(mode.label)));
        QVERIFY(dispatcher.complete(action, AiToolDispatchState::succeeded, result));

        AiAgentTurnBudget replayBudget;
        const auto replay = dispatcher.prepare(
            call, localContext("mode-conversation-" + std::to_string(index), mode.mode), replayBudget);
        QCOMPARE(replay.disposition, AiActionToolDisposition::respond);
        QCOMPARE(replay.outputJson, result);
        QCOMPARE(replayBudget.toolCalls(), std::uint32_t{0});
    }

    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget longBudget;
    const std::string longCommand = "Start-Sleep -Milliseconds 350; Write-Output 'ZTERMY_AGENT_LONG_DONE'";
    const auto longPlan = dispatcher.prepare(commandCall("long-command", longCommand),
                                             localContext("shared-input", AiPermissionMode::yolo), longBudget);
    QCOMPARE(longPlan.disposition, AiActionToolDisposition::execute);
    QVERIFY(longPlan.action.has_value());
    const auto longAction = longPlan.action.value_or(ztermy::ai::AiTerminalAction{});
    session.queueInput(QByteArray::fromStdString(longAction.command + '\r'));
    session.queueInput(QByteArrayLiteral("Write-Output 'ZTERMY_USER_INPUT_DONE'\r"));
    QVERIFY2(waitUntil([&observer] {
                 return blockFor(observer, "Write-Output 'ZTERMY_USER_INPUT_DONE'").has_value();
             }),
             "User input queued during the Agent command did not execute");
    const auto agentBlock = blockFor(observer, longCommand);
    const auto userBlock = blockFor(observer, "Write-Output 'ZTERMY_USER_INPUT_DONE'");
    QVERIFY(agentBlock.has_value());
    QVERIFY(userBlock.has_value());
    const auto agentBlockValue = agentBlock.value_or(CommandBlock{});
    const auto userBlockValue = userBlock.value_or(CommandBlock{});
    QVERIFY(agentBlockValue.id < userBlockValue.id);

    const std::string interactiveCommand =
        "$answer = Read-Host 'AgentPrompt'; Write-Output \"ZTERMY_INTERACTIVE_$answer\"";
    AiAgentTurnBudget interactiveBudget;
    const auto interactivePlan =
        dispatcher.prepare(commandCall("interactive-command", interactiveCommand),
                           localContext("shared-input", AiPermissionMode::yolo), interactiveBudget);
    QCOMPARE(interactivePlan.disposition, AiActionToolDisposition::execute);
    session.queueInput(QByteArray::fromStdString(interactiveCommand + '\r'));
    QVERIFY2(waitUntil([&observer, &interactiveCommand] {
                 return blockFor(observer, interactiveCommand, CommandBlockState::running).has_value();
             }),
             "The interactive Agent command did not enter the running state");

    const auto ptyPlan = dispatcher.prepare(ptyCall("interactive-answer", "agent-answer"),
                                            localContext("shared-input", AiPermissionMode::yolo), interactiveBudget);
    QCOMPARE(ptyPlan.disposition, AiActionToolDisposition::execute);
    QVERIFY(ptyPlan.action.has_value());
    const auto ptyAction = ptyPlan.action.value_or(ztermy::ai::AiTerminalAction{});
    session.queueInput(QByteArray::fromStdString(ptyAction.ptyData + '\r'));
    QVERIFY2(waitUntil([&observer, &interactiveCommand] {
                 return blockFor(observer, interactiveCommand).has_value();
             }),
             "The interactive Agent command did not finish after PTY input");
    const auto interactiveBlock = blockFor(observer, interactiveCommand);
    QVERIFY(interactiveBlock.has_value());
    const auto interactiveBlockValue = interactiveBlock.value_or(CommandBlock{});
    const std::string output(reinterpret_cast<const char *>(interactiveBlockValue.retainedOutput.data()),
                             interactiveBlockValue.retainedOutput.size());
    QVERIFY(output.contains("ZTERMY_INTERACTIVE_agent-answer"));
}

void AiAgentScenarioTests::appliesSshProfileMutationModeMatrix()
{
    struct ModeCase final
    {
        AiPermissionMode mode;
        AiActionToolDisposition disposition;
    };
    constexpr std::array modes{
        ModeCase{.mode = AiPermissionMode::readOnly, .disposition = AiActionToolDisposition::respond},
        ModeCase{.mode = AiPermissionMode::ask, .disposition = AiActionToolDisposition::awaitApproval},
        ModeCase{.mode = AiPermissionMode::automatic, .disposition = AiActionToolDisposition::execute},
        ModeCase{.mode = AiPermissionMode::yolo, .disposition = AiActionToolDisposition::execute},
    };

    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        const auto &mode = modes[index];
        AiActionToolDispatcher dispatcher;
        AiAgentTurnBudget budget;
        const AiActionToolContext sshContext{.conversationId = "ssh-mode-" + std::to_string(index),
                                             .turnId = 9,
                                             .target = {.sessionId = "ssh-session", .sessionGeneration = 4},
                                             .permissionMode = mode.mode,
                                             .profileId = "saved-ssh-profile"};
        const AiToolCall upload{
            .id = "ssh-upload-" + std::to_string(index),
            .name = "queue_sftp_upload",
            .argumentsJson =
                R"({"session_id":"ssh-session","session_generation":4,"local_path":"C:/Temp/report.txt","remote_path":"/tmp/report.txt"})"};
        const auto plan = dispatcher.prepare(upload, sshContext, budget);
        QCOMPARE(plan.disposition, mode.disposition);
        if (mode.mode == AiPermissionMode::readOnly)
        {
            QCOMPARE(jsonObject(plan.outputJson)
                         .value(QStringLiteral("error"))
                         .toObject()
                         .value(QStringLiteral("code"))
                         .toString(),
                     QStringLiteral("permission_denied"));
        }
        else
        {
            QVERIFY(plan.action.has_value());
            const auto action = plan.action.value_or(ztermy::ai::AiTerminalAction{});
            QCOMPARE(action.target.sessionId, std::string("ssh-session"));
            QCOMPARE(action.target.sessionGeneration, std::uint64_t{4});
        }
    }
}

} // namespace

QTEST_GUILESS_MAIN(AiAgentScenarioTests)

#include "ai_agent_scenario_tests.moc"
