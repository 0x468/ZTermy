#include "application/ai/AiTerminalFrameTool.h"

#include <QtTest/QTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
using ztermy::ai::AiSessionTarget;
using ztermy::ai::AiTerminalFrameDelta;
using ztermy::ai::AiTerminalFrameLine;
using ztermy::ai::AiTerminalFrameTool;

[[nodiscard]] AiSessionTarget target()
{
    return {.sessionId = "s", .sessionGeneration = 2};
}

class AiTerminalFrameToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesAndParsesStrictContracts();
    void evaluatesConditionsAndSerializesFrames();
};

void AiTerminalFrameToolTests::publishesAndParsesStrictContracts()
{
    QCOMPARE(AiTerminalFrameTool::readDefinition().name, std::string("read_terminal_frame"));
    QCOMPARE(AiTerminalFrameTool::waitDefinition().name, std::string("wait_terminal_frame"));
    const auto readDefinition = AiTerminalFrameTool::readDefinition();
    const auto waitDefinition = AiTerminalFrameTool::waitDefinition();
    QVERIFY(readDefinition.parametersJson.find("session_id") == std::string::npos);
    QVERIFY(waitDefinition.parametersJson.find("session_generation") == std::string::npos);
    auto read = AiTerminalFrameTool::parseRead(R"({"after_revision":7})", target());
    QVERIFY(read.has_value());
    QCOMPARE(read->afterRevision, std::uint64_t{7});
    auto wait = AiTerminalFrameTool::parseWait(
        R"({"after_revision":7,"condition":"idle","idle_ms":500,"timeout_ms":3000})", target());
    QVERIFY(wait.has_value());
    QCOMPARE(wait->idleMilliseconds, std::uint32_t{500});
    auto defaultWait = AiTerminalFrameTool::parseWait(R"({"after_revision":7})", target());
    QVERIFY(defaultWait.has_value());
    QCOMPARE(defaultWait->condition, ztermy::ai::AiTerminalFrameWaitCondition::changed);
    QCOMPARE(defaultWait->idleMilliseconds, std::uint32_t{0});
    QCOMPARE(defaultWait->timeoutMilliseconds, std::uint32_t{30'000});
    auto defaultIdle = AiTerminalFrameTool::parseWait(R"({"after_revision":7,"condition":"idle"})", target());
    QVERIFY(defaultIdle.has_value());
    QCOMPARE(defaultIdle->idleMilliseconds, std::uint32_t{750});
    QVERIFY(!AiTerminalFrameTool::parseWait(R"({"after_revision":7,"condition":"idle","idle_ms":0,"timeout_ms":3000})",
                                            target())
                 .has_value());
    QVERIFY(!AiTerminalFrameTool::parseWait(R"({"after_revision":7,"condition":"changed","timeout_ms":0})", target())
                 .has_value());
}

void AiTerminalFrameToolTests::evaluatesConditionsAndSerializesFrames()
{
    const AiTerminalFrameDelta frame{.revision = 8,
                                     .baseRevision = 7,
                                     .changedUtcMs = 10,
                                     .idleMilliseconds = 750,
                                     .columns = 80,
                                     .rows = 24,
                                     .cursorColumn = 3,
                                     .cursorVisible = true,
                                     .alternateScreen = true,
                                     .droppedOutputObservations = 2,
                                     .full = false,
                                     .lines = {AiTerminalFrameLine{.index = 2, .text = "changed"}}};
    auto changed = AiTerminalFrameTool::parseWait(
        R"({"after_revision":7,"condition":"changed","idle_ms":0,"timeout_ms":3000})", target());
    auto idle = AiTerminalFrameTool::parseWait(
        R"({"after_revision":8,"condition":"idle","idle_ms":500,"timeout_ms":3000})", target());
    QVERIFY(changed.has_value() && AiTerminalFrameTool::satisfied(*changed, frame));
    QVERIFY(idle.has_value() && AiTerminalFrameTool::satisfied(*idle, frame));
    const auto capability = ztermy::ai::AiTerminalCapabilityAdapter::describe(
        "pwsh", ztermy::terminal::TerminalSemanticCapability::rich, true);
    const auto result =
        QJsonDocument::fromJson(QByteArray::fromStdString(AiTerminalFrameTool::result(frame, "user", capability)))
            .object();
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    const auto value = result.value(QStringLiteral("frame")).toObject();
    QVERIFY(value.value(QStringLiteral("alternate_screen")).toBool());
    QCOMPARE(value.value(QStringLiteral("dropped_output_observations")).toInt(), 2);
    QVERIFY(value.value(QStringLiteral("untrusted_evidence")).toBool());
    QCOMPARE(value.value(QStringLiteral("control_owner")).toString(), QStringLiteral("user"));
    QCOMPARE(value.value(QStringLiteral("capability")).toObject().value(QStringLiteral("semantic_quality")).toString(),
             QStringLiteral("rich_verified"));
    QCOMPARE(value.value(QStringLiteral("lines")).toArray().size(), 1);
}
} // namespace

QTEST_GUILESS_MAIN(AiTerminalFrameToolTests)

#include "ai_terminal_frame_tool_tests.moc"
