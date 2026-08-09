#include "application/sftp/TransferExecutor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

ztermy::ssh::SshTransportError invalidState()
{
    return ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::InvalidState};
}

class MemorySftpClient final : public ztermy::sftp::SftpClient
{
public:
    std::expected<std::string, ztermy::ssh::SshTransportError> canonicalizePath(const std::string_view remotePath,
                                                                                const std::stop_token &) override
    {
        return std::string(remotePath);
    }

    std::map<std::string, std::vector<char>> files;
    std::vector<std::string> removedPaths;

    std::expected<std::vector<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    listDirectory(const std::string_view remotePath, const std::stop_token &) override
    {
        std::vector<ztermy::sftp::DirectoryEntry> result;
        const std::string prefix = remotePath == "/" ? "/" : std::string(remotePath) + "/";
        for (const auto &[path, contents] : files)
        {
            if (!path.starts_with(prefix))
            {
                continue;
            }
            const std::string name = path.substr(prefix.size());
            if (name.find('/') != std::string::npos)
            {
                continue;
            }
            result.push_back(ztermy::sftp::DirectoryEntry{
                .name = name,
                .remotePath = path,
                .type = ztermy::sftp::EntryType::RegularFile,
                .size = contents.size(),
            });
        }
        return result;
    }

    std::expected<std::optional<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    statEntry(const std::string_view remotePath, const std::stop_token &) override
    {
        const auto found = files.find(std::string(remotePath));
        if (found == files.end())
        {
            return std::optional<ztermy::sftp::DirectoryEntry>{};
        }
        const std::size_t separator = found->first.find_last_of('/');
        return std::optional<ztermy::sftp::DirectoryEntry>{ztermy::sftp::DirectoryEntry{
            .name = found->first.substr(separator + 1),
            .remotePath = found->first,
            .type = ztermy::sftp::EntryType::RegularFile,
            .size = found->second.size(),
        }};
    }

    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view,
                                                                        const std::stop_token &) override
    {
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(const std::string_view sourcePath,
                                                                    const std::string_view destinationPath,
                                                                    const bool replace,
                                                                    const std::stop_token &) override
    {
        const auto source = files.find(std::string(sourcePath));
        if (source == files.end() || (!replace && files.contains(std::string(destinationPath))))
        {
            return std::unexpected(invalidState());
        }
        files[std::string(destinationPath)] = std::move(source->second);
        files.erase(source);
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> removeEntry(const std::string_view remotePath, const bool,
                                                                    const std::stop_token &) override
    {
        removedPaths.emplace_back(remotePath);
        files.erase(std::string(remotePath));
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(const std::string_view remotePath,
                                                                        const std::stop_token &) override
    {
        if (m_open || !files.contains(std::string(remotePath)))
        {
            return std::unexpected(invalidState());
        }
        m_open = true;
        m_writing = false;
        m_path = remotePath;
        m_offset = 0;
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError>
    openFileForWrite(const std::string_view remotePath, const bool replace, const std::stop_token &) override
    {
        if (m_open || (!replace && files.contains(std::string(remotePath))))
        {
            return std::unexpected(invalidState());
        }
        m_open = true;
        m_writing = true;
        m_path = remotePath;
        m_pending.clear();
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForResume(const std::string_view remotePath,
                                                                          const std::stop_token &) override
    {
        if (m_open || !files.contains(std::string(remotePath)))
        {
            return std::unexpected(invalidState());
        }
        m_open = true;
        m_writing = true;
        m_path = remotePath;
        m_pending = files.at(m_path);
        m_offset = m_pending.size();
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> seekFile(const std::uint64_t offset) override
    {
        if (!m_open || (m_writing && offset != m_pending.size()) || (!m_writing && offset > files.at(m_path).size()))
        {
            return std::unexpected(invalidState());
        }
        m_offset = static_cast<std::size_t>(offset);
        return {};
    }

    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(const std::span<char> output,
                                                                        const std::stop_token &) override
    {
        if (!m_open || m_writing)
        {
            return std::unexpected(invalidState());
        }
        const auto &contents = files.at(m_path);
        const std::size_t count = std::min(output.size(), contents.size() - m_offset);
        std::ranges::copy_n(contents.begin() + static_cast<std::ptrdiff_t>(m_offset),
                            static_cast<std::ptrdiff_t>(count), output.begin());
        m_offset += count;
        return count;
    }

    std::expected<void, ztermy::ssh::SshTransportError> writeFile(const std::span<const char> input,
                                                                  const std::stop_token &) override
    {
        if (!m_open || !m_writing)
        {
            return std::unexpected(invalidState());
        }
        m_pending.insert(m_pending.end(), input.begin(), input.end());
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override
    {
        if (!m_open)
        {
            return std::unexpected(invalidState());
        }
        if (m_writing)
        {
            files[m_path] = std::move(m_pending);
        }
        m_open = false;
        m_writing = false;
        m_path.clear();
        return {};
    }

private:
    std::string m_path;
    std::vector<char> m_pending;
    std::size_t m_offset = 0;
    bool m_open = false;
    bool m_writing = false;
};

std::vector<char> bytes(const std::string_view text)
{
    return {text.begin(), text.end()};
}

bool writeLocal(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray readLocal(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}

ztermy::sftp::TransferTask downloadTask(const QString &destination)
{
    return ztermy::sftp::TransferTask{
        .id = "download-1",
        .endpointId = "host-1",
        .displayName = "file.txt",
        .sourcePath = "/remote/file.txt",
        .destinationPath = destination.toUtf8().toStdString(),
        .direction = ztermy::sftp::TransferDirection::Download,
    };
}

ztermy::sftp::TransferTask uploadTask(const QString &source)
{
    return ztermy::sftp::TransferTask{
        .id = "upload-1",
        .endpointId = "host-1",
        .displayName = "file.txt",
        .sourcePath = source.toUtf8().toStdString(),
        .destinationPath = "/remote/file.txt",
        .direction = ztermy::sftp::TransferDirection::Upload,
    };
}

class TransferExecutorTests final : public QObject
{
    Q_OBJECT

private slots:
    void reportsDownloadConflictWithoutTouchingDestination();
    void atomicallyReplacesDownloadAndReportsProgress();
    void cancellationPreservesExistingDownload();
    void uploadsThroughTemporaryRemotePath();
    void uploadConflictWaitsForExplicitDecision();
    void cancelledUploadRemovesTemporaryRemoteFile();
    void retryRemovesStaleTemporaryUpload();
    void pausesAndResumesDownloadFromPreservedPartial();
    void pausesAndResumesUploadFromPreservedPartial();
};

void TransferExecutorTests::reportsDownloadConflictWithoutTouchingDestination()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("file.txt"));
    QVERIFY(writeLocal(destination, QByteArray("old")));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = bytes("new");

    const auto result = ztermy::sftp::executeTransfer(client, downloadTask(destination), {}, {}, {});

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::NeedsAttention);
    QVERIFY(result.conflict.has_value());
    QCOMPARE(readLocal(destination), QByteArray("old"));
}

void TransferExecutorTests::atomicallyReplacesDownloadAndReportsProgress()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("file.txt"));
    QVERIFY(writeLocal(destination, QByteArray("old")));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = bytes("new contents");
    std::uint64_t reported = 0;

    const auto result = ztermy::sftp::executeTransfer(
        client, downloadTask(destination), {.conflictAction = ztermy::sftp::ConflictAction::Replace},
        [&reported](const std::uint64_t transferred, const std::uint64_t, const std::uint64_t) {
            reported = transferred;
        },
        {});

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::Completed);
    QCOMPARE(readLocal(destination), QByteArray("new contents"));
    QCOMPARE(reported, std::uint64_t{12});
}

void TransferExecutorTests::cancellationPreservesExistingDownload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("file.txt"));
    QVERIFY(writeLocal(destination, QByteArray("old")));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = std::vector<char>(600000, 'x');
    std::stop_source cancellation;

    const auto result = ztermy::sftp::executeTransfer(
        client, downloadTask(destination), {.conflictAction = ztermy::sftp::ConflictAction::Replace},
        [&cancellation](const std::uint64_t, const std::uint64_t, const std::uint64_t) {
            cancellation.request_stop();
        },
        cancellation.get_token());

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::Cancelled);
    QCOMPARE(readLocal(destination), QByteArray("old"));
}

void TransferExecutorTests::uploadsThroughTemporaryRemotePath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source, QByteArray("uploaded")));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = bytes("old");

    const auto result = ztermy::sftp::executeTransfer(
        client, uploadTask(source), {.conflictAction = ztermy::sftp::ConflictAction::Replace}, {}, {});

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::Completed);
    QCOMPARE(client.files.at("/remote/file.txt"), bytes("uploaded"));
    QCOMPARE(client.files.size(), std::size_t{1});
}

void TransferExecutorTests::uploadConflictWaitsForExplicitDecision()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source, QByteArray("uploaded")));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = bytes("old");

    const auto result = ztermy::sftp::executeTransfer(client, uploadTask(source), {}, {}, {});

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::NeedsAttention);
    QCOMPARE(client.files.at("/remote/file.txt"), bytes("old"));
    QCOMPARE(client.files.size(), std::size_t{1});
}

void TransferExecutorTests::cancelledUploadRemovesTemporaryRemoteFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.bin"));
    QVERIFY(writeLocal(source, QByteArray(600000, 'x')));
    MemorySftpClient client;
    std::stop_source cancellation;

    const auto result = ztermy::sftp::executeTransfer(
        client, uploadTask(source), {},
        [&cancellation](const std::uint64_t, const std::uint64_t, const std::uint64_t) {
            cancellation.request_stop();
        },
        cancellation.get_token());

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::Cancelled);
    QVERIFY(client.files.empty());
    QCOMPARE(client.removedPaths.size(), std::size_t{1});
    QVERIFY(client.removedPaths.front().starts_with("/remote/file.txt.ztermy-part-"));
}

void TransferExecutorTests::retryRemovesStaleTemporaryUpload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("upload.txt"));
    QVERIFY(writeLocal(source, QByteArrayLiteral("replacement")));
    const auto task = uploadTask(source);
    const std::string temporaryPath =
        task.destinationPath + ".ztermy-part-"
        + QCryptographicHash::hash(QByteArray::fromStdString(task.id), QCryptographicHash::Sha256)
              .toHex()
              .first(16)
              .toStdString();
    MemorySftpClient client;
    client.files[temporaryPath] = bytes("stale-partial");

    const auto result = ztermy::sftp::executeTransfer(client, task, {}, {}, {});

    QCOMPARE(result.kind, ztermy::sftp::TransferExecutionResultKind::Completed);
    QCOMPARE(client.files.at(task.destinationPath), bytes("replacement"));
    QVERIFY(std::ranges::find(client.removedPaths, temporaryPath) != client.removedPaths.end());
    QVERIFY(!client.files.contains(temporaryPath));
}

void TransferExecutorTests::pausesAndResumesDownloadFromPreservedPartial()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("resume.bin"));
    MemorySftpClient client;
    client.files["/remote/file.txt"] = std::vector<char>(600000, 'd');
    auto task = downloadTask(destination);
    task.totalBytes = 600000;
    std::stop_source pause;

    const auto paused = ztermy::sftp::executeTransfer(
        client, task, {},
        [&pause](const std::uint64_t, const std::uint64_t, const std::uint64_t) {
            pause.request_stop();
        },
        pause.get_token(),
        [] {
            return true;
        });
    QCOMPARE(paused.kind, ztermy::sftp::TransferExecutionResultKind::Paused);
    QVERIFY(paused.transferredBytes > 0);
    QVERIFY(!QFileInfo::exists(destination));

    task.transferredBytes = paused.transferredBytes;
    const auto completed = ztermy::sftp::executeTransfer(client, task, {}, {}, {});
    QCOMPARE(completed.kind, ztermy::sftp::TransferExecutionResultKind::Completed);
    QCOMPARE(readLocal(destination), QByteArray(600000, 'd'));
}

void TransferExecutorTests::pausesAndResumesUploadFromPreservedPartial()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("resume.bin"));
    QVERIFY(writeLocal(source, QByteArray(600000, 'u')));
    MemorySftpClient client;
    auto task = uploadTask(source);
    task.totalBytes = 600000;
    std::stop_source pause;

    const auto paused = ztermy::sftp::executeTransfer(
        client, task, {},
        [&pause](const std::uint64_t, const std::uint64_t, const std::uint64_t) {
            pause.request_stop();
        },
        pause.get_token(),
        [] {
            return true;
        });
    QCOMPARE(paused.kind, ztermy::sftp::TransferExecutionResultKind::Paused);
    QVERIFY(paused.transferredBytes > 0);

    task.transferredBytes = paused.transferredBytes;
    const auto completed = ztermy::sftp::executeTransfer(client, task, {}, {}, {});
    QCOMPARE(completed.kind, ztermy::sftp::TransferExecutionResultKind::Completed);
    QCOMPARE(client.files.at(task.destinationPath), std::vector<char>(600000, 'u'));
}

} // namespace

QTEST_GUILESS_MAIN(TransferExecutorTests)

#include "transfer_executor_tests.moc"
