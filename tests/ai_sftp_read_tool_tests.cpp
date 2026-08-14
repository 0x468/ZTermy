#include "application/ai/AiSftpReadTool.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiSftpReadTool;
using ztermy::ai::AiSessionTarget;

[[nodiscard]] AiSessionTarget target()
{
    return {.sessionId = "session-1", .sessionGeneration = 4};
}

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

class AiSftpReadToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictDefinition();
    void parsesNormalizedBoundedRequests();
    void rejectsUnsafeOrOversizedRequests();
    void encodesUntrustedResults();
};

void AiSftpReadToolTests::publishesStrictDefinition()
{
    const auto definition = AiSftpReadTool::definition();
    QCOMPARE(definition.name, std::string("read_sftp_file"));
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(definition.parametersJson)).object();
    QVERIFY(!schema.value("additionalProperties").toBool(true));
}

void AiSftpReadToolTests::parsesNormalizedBoundedRequests()
{
    const auto parsed = AiSftpReadTool::parse(
        R"({"remote_path":"/home/test/../file.txt","max_bytes":1024,"encoding":"utf-8"})", target());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->target.sessionId, std::string("session-1"));
    QCOMPARE(parsed->target.sessionGeneration, std::uint64_t{4});
    QCOMPARE(parsed->remotePath, std::string("/home/file.txt"));
    QCOMPARE(parsed->maximumBytes, std::size_t{1024});
}

void AiSftpReadToolTests::rejectsUnsafeOrOversizedRequests()
{
    QVERIFY(
        !AiSftpReadTool::parse(R"({"remote_path":"../../secret","max_bytes":1,"encoding":"utf-8"})", target())
             .has_value());
    QVERIFY(
        !AiSftpReadTool::parse(R"({"remote_path":"/file","max_bytes":32769,"encoding":"utf-8"})", target())
             .has_value());
    QVERIFY(
        !AiSftpReadTool::parse(R"({"remote_path":"/file","max_bytes":1,"encoding":"gb18030"})", target())
             .has_value());
}

void AiSftpReadToolTests::encodesUntrustedResults()
{
    const auto request =
        AiSftpReadTool::parse(R"({"remote_path":"/file","max_bytes":8,"encoding":"base64"})", target());
    QVERIFY(request.has_value());
    auto result = object(AiSftpReadTool::result(*request, QByteArray("abc"), true));
    QVERIFY(result.value("ok").toBool());
    const auto file = result.value("file").toObject();
    QCOMPARE(file.value("content").toString(), QStringLiteral("YWJj"));
    QVERIFY(file.value("truncated").toBool());
    QVERIFY(file.value("untrusted_evidence").toBool());
    QVERIFY(!file.contains("session_id"));
    QVERIFY(!file.contains("session_generation"));

    auto utf8Request = *request;
    utf8Request.encoding = "utf-8";
    result = object(AiSftpReadTool::result(utf8Request, QByteArray::fromHex("ff"), false));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_encoding"));
}

} // namespace

QTEST_GUILESS_MAIN(AiSftpReadToolTests)

#include "ai_sftp_read_tool_tests.moc"
