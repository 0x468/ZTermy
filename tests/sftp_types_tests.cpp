#include "domain/sftp/SftpTypes.h"

#include <QtTest/QTest>

namespace
{

class SftpTypesTests final : public QObject
{
    Q_OBJECT

private slots:
    void normalizesPosixPaths();
    void rejectsUnsafePathsAndNames();
    void joinsAndFindsParents();
    void recognizesHiddenNames();
    void validatesTransferTasksAndTransitions();
    void restrictsConflictReplacement();
};

void SftpTypesTests::normalizesPosixPaths()
{
    QCOMPARE(ztermy::sftp::normalizeRemotePath("/"), std::string{"/"});
    QCOMPARE(ztermy::sftp::normalizeRemotePath("//var///log/./app/../"), std::string{"/var/log"});
    QCOMPARE(ztermy::sftp::normalizeRemotePath("/目录/文件"), std::string{"/目录/文件"});
    QCOMPARE(ztermy::sftp::normalizeRemotePath("/back\\slash"), std::string{"/back\\slash"});
}

void SftpTypesTests::rejectsUnsafePathsAndNames()
{
    QCOMPARE(ztermy::sftp::normalizeRemotePath("").error(), ztermy::sftp::RemotePathError::Empty);
    QCOMPARE(ztermy::sftp::normalizeRemotePath("relative").error(), ztermy::sftp::RemotePathError::NotAbsolute);
    QCOMPARE(ztermy::sftp::normalizeRemotePath("/../../etc").error(),
             ztermy::sftp::RemotePathError::TraversesAboveRoot);

    QVERIFY(!ztermy::sftp::validRemoteName(""));
    QVERIFY(!ztermy::sftp::validRemoteName("."));
    QVERIFY(!ztermy::sftp::validRemoteName(".."));
    QVERIFY(!ztermy::sftp::validRemoteName("a/b"));
    QVERIFY(ztermy::sftp::validRemoteName("文件.txt"));
}

void SftpTypesTests::joinsAndFindsParents()
{
    QCOMPARE(ztermy::sftp::joinRemotePath("/", "home"), std::string{"/home"});
    QCOMPARE(ztermy::sftp::joinRemotePath("/var//log/", "app.log"), std::string{"/var/log/app.log"});
    QCOMPARE(ztermy::sftp::parentRemotePath("/var/log/app.log"), std::string{"/var/log"});
    QCOMPARE(ztermy::sftp::parentRemotePath("/home"), std::string{"/"});
    QCOMPARE(ztermy::sftp::parentRemotePath("/"), std::string{"/"});
}

void SftpTypesTests::recognizesHiddenNames()
{
    QVERIFY(ztermy::sftp::hiddenRemoteName(".config"));
    QVERIFY(!ztermy::sftp::hiddenRemoteName("file"));
    QVERIFY(!ztermy::sftp::hiddenRemoteName("."));
    QVERIFY(!ztermy::sftp::hiddenRemoteName(".."));
}

void SftpTypesTests::validatesTransferTasksAndTransitions()
{
    ztermy::sftp::TransferTask task{
        .id = "task-1",
        .endpointId = "profile-1",
        .displayName = "archive.zip",
        .sourcePath = "C:\\Temp\\archive.zip",
        .destinationPath = "/tmp/archive.zip",
        .direction = ztermy::sftp::TransferDirection::Upload,
        .totalBytes = 100,
        .transferredBytes = 20,
    };
    QVERIFY(ztermy::sftp::validTransferTask(task));
    task.transferredBytes = 101;
    QVERIFY(!ztermy::sftp::validTransferTask(task));

    QVERIFY(ztermy::sftp::canTransition(ztermy::sftp::TransferStatus::Queued, ztermy::sftp::TransferStatus::Running));
    QVERIFY(ztermy::sftp::canTransition(ztermy::sftp::TransferStatus::Running,
                                        ztermy::sftp::TransferStatus::NeedsAttention));
    QVERIFY(ztermy::sftp::canTransition(ztermy::sftp::TransferStatus::Failed, ztermy::sftp::TransferStatus::Queued));
    QVERIFY(
        !ztermy::sftp::canTransition(ztermy::sftp::TransferStatus::Completed, ztermy::sftp::TransferStatus::Running));
}

void SftpTypesTests::restrictsConflictReplacement()
{
    ztermy::sftp::FileConflict conflict{
        .taskId = "task-1",
        .sourcePath = "C:\\Temp\\a.txt",
        .destinationPath = "/tmp/a.txt",
        .sourceType = ztermy::sftp::EntryType::RegularFile,
        .destinationType = ztermy::sftp::EntryType::RegularFile,
    };
    QVERIFY(ztermy::sftp::canReplaceConflict(conflict));

    conflict.destinationType = ztermy::sftp::EntryType::Directory;
    QVERIFY(!ztermy::sftp::canReplaceConflict(conflict));

    conflict.sourceType = ztermy::sftp::EntryType::SymbolicLink;
    conflict.destinationType = ztermy::sftp::EntryType::SymbolicLink;
    QVERIFY(!ztermy::sftp::canReplaceConflict(conflict));
}

} // namespace

QTEST_GUILESS_MAIN(SftpTypesTests)

#include "sftp_types_tests.moc"
