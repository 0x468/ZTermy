#include "application/sftp/SftpDirectoryModel.h"

#include <QSignalSpy>
#include <QtTest/QTest>

#include <memory>
#include <vector>

namespace
{

class SftpDirectoryModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void sortsDirectoriesAndFiltersHiddenEntries();
    void filtersNamesCaseInsensitively();
    void tracksSelectionAndClearsItOnRefresh();
    void exposesParentDirectoryOutsideRoot();
};

ztermy::sftp::DirectoryListingPtr listing()
{
    return std::make_shared<const ztermy::sftp::DirectoryListing>(ztermy::sftp::DirectoryListing{
        ztermy::sftp::DirectoryEntry{
            .name = "zeta.txt",
            .remotePath = "/zeta.txt",
            .type = ztermy::sftp::EntryType::RegularFile,
            .size = 10,
        },
        ztermy::sftp::DirectoryEntry{
            .name = ".secret",
            .remotePath = "/.secret",
            .type = ztermy::sftp::EntryType::RegularFile,
            .size = 5,
        },
        ztermy::sftp::DirectoryEntry{
            .name = "Alpha",
            .remotePath = "/Alpha",
            .type = ztermy::sftp::EntryType::Directory,
        },
    });
}

void SftpDirectoryModelTests::sortsDirectoriesAndFiltersHiddenEntries()
{
    ztermy::sftp::SftpDirectoryModel model;
    model.setEntries(listing());

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), ztermy::sftp::SftpDirectoryModel::NameRole).toString(),
             QStringLiteral("Alpha"));
    QCOMPARE(model.data(model.index(0), ztermy::sftp::SftpDirectoryModel::TypeRole).toString(),
             QStringLiteral("directory"));

    model.setShowHidden(true);
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.showHidden());
}

void SftpDirectoryModelTests::filtersNamesCaseInsensitively()
{
    ztermy::sftp::SftpDirectoryModel model;
    model.setShowHidden(true);
    model.setEntries(listing());
    model.setFilterText(QStringLiteral("ALP"));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.entry(0).value(QStringLiteral("remotePath")).toString(), QStringLiteral("/Alpha"));
}

void SftpDirectoryModelTests::tracksSelectionAndClearsItOnRefresh()
{
    ztermy::sftp::SftpDirectoryModel model;
    QSignalSpy selectionSpy(&model, &ztermy::sftp::SftpDirectoryModel::selectionChanged);
    model.setEntries(listing());
    selectionSpy.clear();

    QVERIFY(model.setSelected(1, true));
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(model.selectedPaths(), QStringList{QStringLiteral("/zeta.txt")});
    QCOMPARE(selectionSpy.count(), 1);

    model.setEntries(listing());
    QCOMPARE(model.selectedCount(), 0);
    QVERIFY(model.selectedPaths().isEmpty());
}

void SftpDirectoryModelTests::exposesParentDirectoryOutsideRoot()
{
    ztermy::sftp::SftpDirectoryModel model;
    model.setEntries(listing(), QStringLiteral("/home/tester/work"));

    QCOMPARE(model.entry(0).value(QStringLiteral("name")).toString(), QStringLiteral(".."));
    QCOMPARE(model.entry(0).value(QStringLiteral("remotePath")).toString(), QStringLiteral("/home/tester"));
    model.setFilterText(QStringLiteral("no-match"));
    QCOMPARE(model.rowCount(), 1);

    model.setFilterText({});
    model.setEntries(listing(), QStringLiteral("/"));
    QCOMPARE(model.entry(0).value(QStringLiteral("name")).toString(), QStringLiteral("Alpha"));
}

} // namespace

QTEST_GUILESS_MAIN(SftpDirectoryModelTests)

#include "sftp_directory_model_tests.moc"
