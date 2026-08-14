#include "application/ai/AiNoteReadTool.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiNoteReadTool;
using ztermy::ai::AiSessionTarget;

[[nodiscard]] AiSessionTarget target()
{
    return {.sessionId = "session-1", .sessionGeneration = 7};
}

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

class AiNoteReadToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictSchema();
    void parsesNormalizedBoundedRequest();
    void rejectsUnsafeAndOversizedRequests();
    void boundsUtf8ResultAndMarksEvidence();
};

void AiNoteReadToolTests::publishesStrictSchema()
{
    const auto definition = AiNoteReadTool::definition();
    QCOMPARE(definition.name, std::string("read_note"));
    const auto schema = object(definition.parametersJson);
    QVERIFY(schema.value("additionalProperties").isBool());
    QVERIFY(!schema.value("additionalProperties").toBool());
}

void AiNoteReadToolTests::parsesNormalizedBoundedRequest()
{
    const auto request = AiNoteReadTool::parse(R"({"path":"ops/./runbook.md","max_bytes":4096})", target());
    QVERIFY(request.has_value());
    QCOMPARE(request->target.sessionId, std::string("session-1"));
    QCOMPARE(request->target.sessionGeneration, std::uint64_t{7});
    QCOMPARE(request->relativePath, QStringLiteral("ops/runbook.md"));
    QCOMPARE(request->maximumBytes, std::size_t{4096});
}

void AiNoteReadToolTests::rejectsUnsafeAndOversizedRequests()
{
    auto request = AiNoteReadTool::parse(R"({"path":"../secret.md","max_bytes":4096})", target());
    QVERIFY(!request.has_value());
    QCOMPARE(object(request.error()).value("error").toObject().value("code").toString(),
             QStringLiteral("invalid_arguments"));

    request = AiNoteReadTool::parse(R"({"path":"note.txt","max_bytes":4096})", target());
    QVERIFY(!request.has_value());

    request = AiNoteReadTool::parse(R"({"path":"note.md","max_bytes":32769})", target());
    QVERIFY(!request.has_value());
}

void AiNoteReadToolTests::boundsUtf8ResultAndMarksEvidence()
{
    const auto request = AiNoteReadTool::parse(R"({"path":"note.md","max_bytes":5})", target());
    QVERIFY(request.has_value());
    const auto result = object(AiNoteReadTool::result(*request, QStringLiteral("你a好")));
    QVERIFY(result.value("ok").toBool());
    const auto note = result.value("note").toObject();
    QCOMPARE(note.value("content").toString(), QStringLiteral("你a"));
    QCOMPARE(note.value("bytes_read").toInt(), 4);
    QVERIFY(note.value("truncated").toBool());
    QVERIFY(note.value("untrusted_evidence").toBool());
    QVERIFY(!note.contains("session_id"));
    QVERIFY(!note.contains("session_generation"));

    const auto redacted =
        object(AiNoteReadTool::result(*request, QStringLiteral("OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz123456")));
    const auto redactedNote = redacted.value("note").toObject();
    QVERIFY(redactedNote.value("redacted").toBool());
    QVERIFY(!redactedNote.value("content").toString().contains(QStringLiteral("secret")));
}

} // namespace

QTEST_GUILESS_MAIN(AiNoteReadToolTests)

#include "ai_note_read_tool_tests.moc"
