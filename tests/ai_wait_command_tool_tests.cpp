#include "application/ai/AiWaitCommandTool.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiSessionTarget;
using ztermy::ai::AiTrackedCommand;
using ztermy::ai::AiTrackedCommandState;
using ztermy::ai::AiWaitCommandTool;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] AiSessionTarget target()
{
    return {.sessionId = "session-1", .sessionGeneration = 3};
}

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

[[nodiscard]] AiTrackedCommand command(const AiTrackedCommandState state)
{
    return AiTrackedCommand{.id = "9:call-1",
                            .conversationId = "conversation-1",
                            .target = {.sessionId = "session-1", .sessionGeneration = 3},
                            .command = "pwd",
                            .blockId = 12,
                            .state = state,
                            .exitStatus =
                                state == AiTrackedCommandState::finished ? std::optional<int>{0} : std::nullopt};
}

class AiWaitCommandToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesAndParsesStrictContract();
    void guidesUnavailableLifecycleToFrameWait();
    void serializesLifecycleTimeoutAndUnknownOutcome();
    void returnsFinishedCommandOutput();
    void capsOversizedFinishedOutput();
};

void AiWaitCommandToolTests::publishesAndParsesStrictContract()
{
    const auto definition = AiWaitCommandTool::definition();
    QCOMPARE(definition.name, std::string("wait_command"));
    QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(definition.parametersJson)).isObject());

    const auto parsed = AiWaitCommandTool::parse(R"({"command_id":"9:call-1","timeout_ms":2500})", target());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->commandId, std::string("9:call-1"));
    QCOMPARE(parsed->target.sessionGeneration, std::uint64_t{3});
    QCOMPARE(parsed->timeoutMilliseconds, std::uint32_t{2500});

    QVERIFY(!AiWaitCommandTool::parse(R"({"command_id":"9:call-1","timeout_ms":120001})", target()).has_value());
    QVERIFY(
        !AiWaitCommandTool::parse(R"({"command_id":"9:call-1","timeout_ms":1,"extra":true})", target()).has_value());

    const auto defaults = AiWaitCommandTool::parse(R"({"command_id":"9:call-1"})", target());
    QVERIFY(defaults.has_value());
    QCOMPARE(defaults->timeoutMilliseconds, std::uint32_t{30'000});
    QVERIFY(definition.parametersJson.find("session_id") == std::string::npos);
    QVERIFY(definition.parametersJson.find("session_generation") == std::string::npos);
}

void AiWaitCommandToolTests::guidesUnavailableLifecycleToFrameWait()
{
    const auto unavailable =
        object(AiWaitCommandTool::accepted("command-1", true, TerminalSemanticCapability::none, std::uint64_t{17}));
    QVERIFY(unavailable.value(QStringLiteral("tracking_registered")).toBool());
    QVERIFY(!unavailable.value(QStringLiteral("lifecycle_tracked")).toBool(true));
    QCOMPARE(unavailable.value(QStringLiteral("lifecycle_quality")).toString(), QStringLiteral("unavailable"));
    QCOMPARE(unavailable.value(QStringLiteral("recommended_wait_tool")).toString(),
             QStringLiteral("wait_terminal_frame"));
    QCOMPARE(unavailable.value(QStringLiteral("frame_wait_strategy")).toString(), QStringLiteral("changed_then_idle"));
    QCOMPARE(unavailable.value(QStringLiteral("recommended_idle_ms")).toInt(), 750);
    QCOMPARE(unavailable.value(QStringLiteral("frame_revision_before_dispatch")).toInteger(), qint64{17});

    const auto rich =
        object(AiWaitCommandTool::accepted("command-2", true, TerminalSemanticCapability::rich, std::uint64_t{18}));
    QVERIFY(rich.value(QStringLiteral("lifecycle_tracked")).toBool());
    QCOMPARE(rich.value(QStringLiteral("recommended_wait_tool")).toString(), QStringLiteral("wait_command"));
}

void AiWaitCommandToolTests::serializesLifecycleTimeoutAndUnknownOutcome()
{
    auto finished = object(AiWaitCommandTool::result(command(AiTrackedCommandState::finished)));
    QVERIFY(finished.value(QStringLiteral("ok")).toBool());
    QCOMPARE(finished.value(QStringLiteral("command")).toObject().value(QStringLiteral("block_id")).toInt(), 12);
    QCOMPARE(finished.value(QStringLiteral("command")).toObject().value(QStringLiteral("exit_status")).toInt(), 0);

    const auto timedOut = object(AiWaitCommandTool::timeout(command(AiTrackedCommandState::running)));
    QVERIFY(!timedOut.value(QStringLiteral("ok")).toBool(true));
    QCOMPARE(timedOut.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("timeout"));

    const auto unknown = object(AiWaitCommandTool::result(command(AiTrackedCommandState::outcomeUnknown)));
    QVERIFY(!unknown.value(QStringLiteral("ok")).toBool(true));
    QCOMPARE(unknown.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("outcome_unknown"));
}

void AiWaitCommandToolTests::returnsFinishedCommandOutput()
{
    auto tracked = command(AiTrackedCommandState::finished);
    tracked.output = "\x1b[32mFilesystem Size Used Avail Use% Mounted on\x1b[0m\r\n"
                     "/dev/sda1 80G 20G 60G 25% /\r\n";
    tracked.outputCoverage = ztermy::terminal::CommandOutputCoverage::complete;

    const auto result = object(AiWaitCommandTool::result(tracked));
    const auto serialized = result.value(QStringLiteral("command")).toObject();
    QCOMPARE(serialized.value(QStringLiteral("output")).toString(),
             QStringLiteral("Filesystem Size Used Avail Use% Mounted on\n/dev/sda1 80G 20G 60G 25% /\n"));
    QVERIFY(serialized.value(QStringLiteral("output_complete")).toBool());
    QCOMPARE(serialized.value(QStringLiteral("omitted_output_bytes")).toInteger(), qint64{0});
}

void AiWaitCommandToolTests::capsOversizedFinishedOutput()
{
    auto tracked = command(AiTrackedCommandState::finished);
    tracked.output = std::string(std::size_t{40} * 1024, 'x');
    tracked.outputCoverage = ztermy::terminal::CommandOutputCoverage::complete;

    const auto result = object(AiWaitCommandTool::result(tracked));
    const auto serialized = result.value(QStringLiteral("command")).toObject();
    const QString output = serialized.value(QStringLiteral("output")).toString();
    // The embedded output must stay far below the 64 KiB tool-output bound so
    // JSON escaping can never push the serialized result over it.
    QVERIFY(output.size() <= qsizetype{24} * 1024);
    QVERIFY(serialized.value(QStringLiteral("output_truncated")).toBool());
    QVERIFY(!serialized.value(QStringLiteral("output_complete")).toBool());
}

} // namespace

QTEST_GUILESS_MAIN(AiWaitCommandToolTests)

#include "ai_wait_command_tool_tests.moc"
