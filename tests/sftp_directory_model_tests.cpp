#include "application/sftp/SftpDirectoryModel.h"

#include <QElapsedTimer>
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
    void selectsVisibleRangesAndSkipsParentEntry();
    void exposesParentDirectoryOutsideRoot();
    void handlesLargeDirectoryWithinBudget();
    void expandsTreeDirectoriesLazilyAndSurfacesErrors();
    void supportsConfigurableSortAndDirectoryGrouping();
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

void SftpDirectoryModelTests::expandsTreeDirectoriesLazilyAndSurfacesErrors()
{
    ztermy::sftp::SftpDirectoryModel model;
    model.setViewMode(QStringLiteral("tree"));
    QSignalSpy requestSpy(&model, &ztermy::sftp::SftpDirectoryModel::treeDirectoryRequested);
    model.setEntries(listing(), QStringLiteral("/"));

    QCOMPARE(model.viewMode(), QStringLiteral("tree"));
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.data(model.index(0), ztermy::sftp::SftpDirectoryModel::ExpandableRole).toBool());
    QVERIFY(model.toggleExpanded(0));
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(requestSpy.front().front().toString(), QStringLiteral("/Alpha"));
    QVERIFY(model.data(model.index(0), ztermy::sftp::SftpDirectoryModel::LoadingRole).toBool());

    const auto children = std::make_shared<const ztermy::sftp::DirectoryListing>(ztermy::sftp::DirectoryListing{
        ztermy::sftp::DirectoryEntry{.name = "nested",
                                     .remotePath = "/Alpha/nested",
                                     .type = ztermy::sftp::EntryType::Directory},
        ztermy::sftp::DirectoryEntry{.name = "readme.txt",
                                     .remotePath = "/Alpha/readme.txt",
                                     .type = ztermy::sftp::EntryType::RegularFile,
                                     .size = 7},
    });
    model.applyTreeEntries(QStringLiteral("/Alpha"), children);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(1), ztermy::sftp::SftpDirectoryModel::DepthRole).toInt(), 1);
    QCOMPARE(model.entry(2).value(QStringLiteral("remotePath")).toString(), QStringLiteral("/Alpha/readme.txt"));

    QVERIFY(model.toggleExpanded(1));
    QCOMPARE(requestSpy.count(), 2);
    model.applyTreeError(QStringLiteral("/Alpha/nested"), QStringLiteral("denied"));
    QCOMPARE(model.data(model.index(1), ztermy::sftp::SftpDirectoryModel::LoadErrorRole).toString(),
             QStringLiteral("denied"));
    QVERIFY(!model.data(model.index(1), ztermy::sftp::SftpDirectoryModel::LoadingRole).toBool());

    QVERIFY(model.toggleExpanded(0));
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.toggleExpanded(0));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(requestSpy.count(), 2);
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

void SftpDirectoryModelTests::selectsVisibleRangesAndSkipsParentEntry()
{
    ztermy::sftp::SftpDirectoryModel model;
    QSignalSpy selectionSpy(&model, &ztermy::sftp::SftpDirectoryModel::selectionChanged);
    model.setEntries(listing(), QStringLiteral("/work"));
    selectionSpy.clear();

    model.selectRange(0, 2);
    QCOMPARE(model.selectedCount(), 2);
    QCOMPARE(model.selectedPaths(), QStringList({QStringLiteral("/Alpha"), QStringLiteral("/zeta.txt")}));
    QCOMPARE(selectionSpy.count(), 1);
    QVERIFY(!model.setSelected(0, true));

    model.selectRange(2, 2);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(model.selectedPaths(), QStringList{QStringLiteral("/zeta.txt")});
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

void SftpDirectoryModelTests::handlesLargeDirectoryWithinBudget()
{
    ztermy::sftp::DirectoryListing entries;
    constexpr std::size_t entryCount = 10'000;
    entries.reserve(entryCount);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        const bool directory = index % 8 == 0;
        const std::string suffix = std::to_string(entryCount - index);
        const std::string name = directory ? "directory-" + suffix : "artifact-" + suffix + ".log";
        entries.push_back(ztermy::sftp::DirectoryEntry{
            .name = name,
            .remotePath = "/var/data/" + name,
            .type = directory ? ztermy::sftp::EntryType::Directory : ztermy::sftp::EntryType::RegularFile,
            .size = index,
        });
    }

    ztermy::sftp::SftpDirectoryModel model;
    const auto listing = std::make_shared<const ztermy::sftp::DirectoryListing>(std::move(entries));
    QElapsedTimer listingTimer;
    listingTimer.start();
    model.setEntries(listing, QStringLiteral("/var/data"));
    const qint64 listingMs = listingTimer.elapsed();

    QCOMPARE(model.rowCount(), static_cast<int>(entryCount + 1));
    QCOMPARE(model.entry(0).value(QStringLiteral("name")).toString(), QStringLiteral(".."));

    QElapsedTimer filterTimer;
    filterTimer.start();
    model.setFilterText(QStringLiteral("artifact-999"));
    const qint64 filterMs = filterTimer.elapsed();

    QVERIFY(model.rowCount() > 1);
    qInfo("V2.3 SFTP budget: %lld ms to sort 10,000 entries; %lld ms to filter", static_cast<long long>(listingMs),
          static_cast<long long>(filterMs));
    QVERIFY2(listingMs <= 5'000, "Large SFTP listing exceeded the V2.3 5 s Debug budget");
    QVERIFY2(filterMs <= 1'000, "Large SFTP filter exceeded the V2.3 1 s Debug budget");
}

void SftpDirectoryModelTests::supportsConfigurableSortAndDirectoryGrouping()
{
    ztermy::sftp::SftpDirectoryModel model;
    model.setShowHidden(true);
    model.setEntries(listing());
    model.setDirectoriesFirst(false);
    model.setSortColumn(QStringLiteral("size"));
    model.setSortAscending(false);

    QCOMPARE(model.entry(0).value(QStringLiteral("name")).toString(), QStringLiteral("zeta.txt"));
    QCOMPARE(model.entry(1).value(QStringLiteral("name")).toString(), QStringLiteral(".secret"));
    QCOMPARE(model.entry(2).value(QStringLiteral("name")).toString(), QStringLiteral("Alpha"));
    QCOMPARE(model.sortColumn(), QStringLiteral("size"));
    QVERIFY(!model.sortAscending());
    QVERIFY(!model.directoriesFirst());
}

} // namespace

QTEST_GUILESS_MAIN(SftpDirectoryModelTests)

#include "sftp_directory_model_tests.moc"
