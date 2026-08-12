#include "application/ai/AiSftpListTool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiSftpListTool;

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

class AiSftpListToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesNormalizedBoundedRequest();
    void rejectsUnsafeAndOversizedRequests();
    void pagesTypedUntrustedEntries();
};

void AiSftpListToolTests::parsesNormalizedBoundedRequest()
{
    const auto request = AiSftpListTool::parse(
        R"({"session_id":"session-1","session_generation":8,"path":"/var/./log","offset":2,"limit":20})");
    QVERIFY(request.has_value());
    QCOMPARE(request->target.sessionId, std::string("session-1"));
    QCOMPARE(request->target.sessionGeneration, std::uint64_t{8});
    QCOMPARE(request->remotePath, std::string("/var/log"));
    QCOMPARE(request->offset, std::size_t{2});
    QCOMPARE(request->limit, std::size_t{20});
}

void AiSftpListToolTests::rejectsUnsafeAndOversizedRequests()
{
    auto request = AiSftpListTool::parse(
        R"({"session_id":"session-1","session_generation":8,"path":"../../etc","offset":0,"limit":20})");
    QVERIFY(!request.has_value());
    request = AiSftpListTool::parse(
        R"({"session_id":"session-1","session_generation":8,"path":"/etc","offset":0,"limit":101})");
    QVERIFY(!request.has_value());
}

void AiSftpListToolTests::pagesTypedUntrustedEntries()
{
    ztermy::sftp::DirectoryListing entries{
        {.name = ".hidden", .remotePath = "/tmp/.hidden", .type = ztermy::sftp::EntryType::RegularFile, .size = 4},
        {.name = "folder", .remotePath = "/tmp/folder", .type = ztermy::sftp::EntryType::Directory}};
    const auto request = AiSftpListTool::parse(
        R"({"session_id":"session-1","session_generation":8,"path":"/tmp","offset":0,"limit":1})");
    QVERIFY(request.has_value());
    const auto result = object(AiSftpListTool::result(*request, entries));
    QVERIFY(result.value("ok").toBool());
    const auto listing = result.value("sftp_directory").toObject();
    QCOMPARE(listing.value("items").toArray().size(), 1);
    QVERIFY(listing.value("items").toArray().at(0).toObject().value("hidden").toBool());
    QVERIFY(listing.value("has_more").toBool());
    QVERIFY(listing.value("untrusted_evidence").toBool());
}

} // namespace

QTEST_GUILESS_MAIN(AiSftpListToolTests)

#include "ai_sftp_list_tool_tests.moc"
