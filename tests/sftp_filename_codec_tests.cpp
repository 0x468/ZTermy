#include "application/sftp/SftpFilenameCodec.h"

#include <QtTest/QTest>

namespace
{

class CapturingClient final : public ztermy::sftp::SftpClient
{
public:
    std::string lastPath;

    std::expected<std::string, ztermy::ssh::SshTransportError> canonicalizePath(const std::string_view path,
                                                                                const std::stop_token &) override
    {
        lastPath = path;
        return std::string("/") + std::string("\xB2\xE2\xCA\xD4", 4);
    }
    std::expected<std::vector<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    listDirectory(const std::string_view path, const std::stop_token &) override
    {
        lastPath = path;
        return std::vector<ztermy::sftp::DirectoryEntry>{ztermy::sftp::DirectoryEntry{
            .name = std::string("\xB2\xE2\xCA\xD4.txt", 8),
            .remotePath = std::string("/\xB2\xE2\xCA\xD4.txt", 9),
            .type = ztermy::sftp::EntryType::RegularFile,
        }};
    }
    std::expected<std::optional<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    statEntry(const std::string_view, const std::stop_token &) override
    {
        return std::optional<ztermy::sftp::DirectoryEntry>{};
    }
    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view path,
                                                                        const std::stop_token &) override
    {
        lastPath = path;
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(std::string_view, std::string_view, bool,
                                                                    const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> removeEntry(std::string_view, bool,
                                                                    const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(std::string_view,
                                                                        const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForWrite(std::string_view, bool,
                                                                         const std::stop_token &) override
    {
        return {};
    }
    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(std::span<char>,
                                                                        const std::stop_token &) override
    {
        return std::size_t{0};
    }
    std::expected<void, ztermy::ssh::SshTransportError> writeFile(std::span<const char>,
                                                                  const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override { return {}; }
};

} // namespace

class SftpFilenameCodecTests final : public QObject
{
    Q_OBJECT

private slots:
    void convertsGb18030PathsAtTheTransportBoundary();
};

void SftpFilenameCodecTests::convertsGb18030PathsAtTheTransportBoundary()
{
    auto raw = std::make_unique<CapturingClient>();
    CapturingClient *capture = raw.get();
    auto client = ztermy::sftp::withFilenameEncoding(std::move(raw), "gb18030");

    QVERIFY(client->createDirectory("/测试", {}));
    QCOMPARE(capture->lastPath, std::string("/\xB2\xE2\xCA\xD4", 5));

    const auto entries = client->listDirectory("/测试", {});
    QVERIFY(entries.has_value());
    QCOMPARE(entries->front().name, std::string("测试.txt"));
    QCOMPARE(entries->front().remotePath, std::string("/测试.txt"));
}

QTEST_GUILESS_MAIN(SftpFilenameCodecTests)

#include "sftp_filename_codec_tests.moc"
