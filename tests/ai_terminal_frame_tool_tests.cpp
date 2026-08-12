#include "application/ai/AiTerminalFrameTool.h"

#include <QtTest/QTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
using ztermy::ai::AiTerminalFrameDelta;
using ztermy::ai::AiTerminalFrameLine;
using ztermy::ai::AiTerminalFrameTool;

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
    auto read = AiTerminalFrameTool::parseRead(R"({"session_id":"s","session_generation":2,"after_revision":7})");
    QVERIFY(read.has_value());
    QCOMPARE(read->afterRevision, std::uint64_t{7});
    auto wait = AiTerminalFrameTool::parseWait(
        R"({"session_id":"s","session_generation":2,"after_revision":7,"condition":"idle","idle_ms":500,"timeout_ms":3000})");
    QVERIFY(wait.has_value());
    QCOMPARE(wait->idleMilliseconds, std::uint32_t{500});
    QVERIFY(
        !AiTerminalFrameTool::parseWait(
             R"({"session_id":"s","session_generation":2,"after_revision":7,"condition":"idle","idle_ms":0,"timeout_ms":3000})")
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
                                     .full = false,
                                     .lines = {AiTerminalFrameLine{.index = 2, .text = "changed"}}};
    auto changed = AiTerminalFrameTool::parseWait(
        R"({"session_id":"s","session_generation":2,"after_revision":7,"condition":"changed","idle_ms":0,"timeout_ms":3000})");
    auto idle = AiTerminalFrameTool::parseWait(
        R"({"session_id":"s","session_generation":2,"after_revision":8,"condition":"idle","idle_ms":500,"timeout_ms":3000})");
    QVERIFY(changed.has_value() && AiTerminalFrameTool::satisfied(*changed, frame));
    QVERIFY(idle.has_value() && AiTerminalFrameTool::satisfied(*idle, frame));
    const auto result =
        QJsonDocument::fromJson(QByteArray::fromStdString(AiTerminalFrameTool::result(frame, "user"))).object();
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    const auto value = result.value(QStringLiteral("frame")).toObject();
    QVERIFY(value.value(QStringLiteral("alternate_screen")).toBool());
    QVERIFY(value.value(QStringLiteral("untrusted_evidence")).toBool());
    QCOMPARE(value.value(QStringLiteral("control_owner")).toString(), QStringLiteral("user"));
    QCOMPARE(value.value(QStringLiteral("lines")).toArray().size(), 1);
}
} // namespace

QTEST_GUILESS_MAIN(AiTerminalFrameToolTests)

#include "ai_terminal_frame_tool_tests.moc"
