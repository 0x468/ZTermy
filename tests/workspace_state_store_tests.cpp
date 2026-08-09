#include "infrastructure/workbench/WorkspaceStateStore.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <string>

class WorkspaceStateStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsEmptyState();
    void savesAndLoadsVersionedNonSecretState();
    void migratesVersionOneWithoutHostCollapseState();
    void migratesVersionTwoWithoutSftpBookmarks();
    void migratesVersionThreeWithoutSftpNavigationPreferences();
    void migratesVersionFourWithoutSftpListingPreferences();
    void migratesVersionFiveWithoutTerminalWorkspaces();
    void togglesAndBoundsSftpBookmarks();
    void rejectsMalformedDuplicateAndInvalidState();
    void rejectsMalformedTerminalWorkspaceTopology();
    void recoversTheLastKnownGoodStateFromBackup();
    void refusesToOverwriteANewerWorkspaceSchema();
};

void WorkspaceStateStoreTests::missingFileLoadsEmptyState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::workbench::WorkspaceStateStore store(directory.filePath(QStringLiteral("workspace.json")));
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->profiles.empty());
}

void WorkspaceStateStoreTests::savesAndLoadsVersionedNonSecretState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/workspace.json"));
    const ztermy::workbench::WorkspaceStateStore store(path);
    ztermy::workbench::WorkspaceState expected;
    expected.profiles.push_back({
        .profileId = "host-a",
        .lastRemotePath = "/var/log",
        .recentRemotePaths = {"/var/log", "/etc"},
        .bookmarkedRemotePaths = {"/srv", "/var/log"},
        .workbenchPage = "sftp",
        .workbenchSide = "right",
        .sftpViewMode = "tree",
        .sftpSortColumn = "size",
        .sftpFilenameEncoding = "gb18030",
        .followTerminalDirectory = true,
        .sftpSortAscending = false,
        .sftpDirectoriesFirst = false,
        .sftpShowModifiedColumn = false,
        .sftpShowSizeColumn = true,
        .sftpShowTypeColumn = true,
        .workbenchWidth = 640.0,
        .composerHeight = 160.0,
    });
    expected.collapsedHostSections = {"recent", "group:Lab"};
    auto workspace = ztermy::workbench::makeSinglePaneTerminalWorkspace("workspace-a", "pane-a",
                                                                        {.id = "intent-a", .title = "PowerShell"});
    workspace.title = "Operations";
    QVERIFY(ztermy::workbench::splitTerminalPane(workspace, "pane-a", "split-a", "pane-b",
                                                 {.id = "intent-b",
                                                  .profileId = "host-a",
                                                  .title = "host-a",
                                                  .kind = ztermy::workbench::TerminalRestoreKind::SshProfile},
                                                 ztermy::workbench::TerminalSplitOrientation::Vertical, 0.5, true));
    expected.terminalWorkspaces.push_back(std::move(workspace));
    expected.activeTerminalWorkspaceId = "workspace-a";

    QVERIFY(store.save(expected).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, expected);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    QVERIFY(payload.contains("schemaVersion"));
    QVERIFY(payload.contains("terminalWorkspaces"));
    QVERIFY(payload.contains("activeTerminalWorkspaceId"));
    QVERIFY(!payload.contains("password"));
    QVERIFY(!payload.contains("secret"));
}

void WorkspaceStateStoreTests::migratesVersionFiveWithoutTerminalWorkspaces()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"schemaVersion":5,"profiles":[],"collapsedHostSections":[]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->terminalWorkspaces.empty());
    QVERIFY(loaded->activeTerminalWorkspaceId.empty());
}

void WorkspaceStateStoreTests::migratesVersionThreeWithoutSftpNavigationPreferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schemaVersion":3,"profiles":[{"profileId":"host","lastRemotePath":"/srv","recentRemotePaths":["/srv"],"bookmarkedRemotePaths":[],"workbenchPage":"sftp","workbenchSide":"left","workbenchWidth":520,"composerHeight":132}],"collapsedHostSections":[]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->profiles.front().sftpViewMode, std::string("list"));
    QVERIFY(!loaded->profiles.front().followTerminalDirectory);
}

void WorkspaceStateStoreTests::migratesVersionFourWithoutSftpListingPreferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray payload =
        R"({"schemaVersion":4,"profiles":[{"profileId":"host","lastRemotePath":"/srv","recentRemotePaths":["/srv"],"bookmarkedRemotePaths":["/srv/log"],"workbenchPage":"sftp","workbenchSide":"left","workbenchWidth":520,"composerHeight":132,"sftpViewMode":"tree","followTerminalDirectory":true}],"collapsedHostSections":[]})";
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    const auto loaded = ztermy::workbench::WorkspaceStateStore(path).load();
    QVERIFY(loaded);
    QCOMPARE(loaded->profiles.size(), std::size_t{1});
    QCOMPARE(loaded->profiles.front().sftpViewMode, std::string("tree"));
    QVERIFY(loaded->profiles.front().followTerminalDirectory);
    QCOMPARE(loaded->profiles.front().sftpSortColumn, std::string("name"));
    QCOMPARE(loaded->profiles.front().sftpFilenameEncoding, std::string("utf-8"));
    QVERIFY(loaded->profiles.front().sftpDirectoriesFirst);
}

void WorkspaceStateStoreTests::migratesVersionTwoWithoutSftpBookmarks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schemaVersion":2,"profiles":[{"profileId":"host","lastRemotePath":"/srv","recentRemotePaths":["/srv"],"workbenchPage":"sftp","workbenchSide":"left","workbenchWidth":520,"composerHeight":132}],"collapsedHostSections":[]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->profiles.size(), std::size_t{1});
    QVERIFY(loaded->profiles.front().bookmarkedRemotePaths.empty());
}

void WorkspaceStateStoreTests::migratesVersionOneWithoutHostCollapseState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schemaVersion":1,"profiles":[{"profileId":"host","lastRemotePath":"/","recentRemotePaths":[],"workbenchPage":"history","workbenchSide":"left","workbenchWidth":520,"composerHeight":132}]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->profiles.size(), std::size_t{1});
    QVERIFY(loaded->collapsedHostSections.empty());
}

void WorkspaceStateStoreTests::togglesAndBoundsSftpBookmarks()
{
    ztermy::workbench::ProfileWorkspaceState state{.profileId = "host"};
    QVERIFY(ztermy::workbench::toggleBookmarkedRemotePath(state, "/home/test"));
    QVERIFY(ztermy::workbench::remotePathBookmarked(state, "/home/test"));
    QVERIFY(!ztermy::workbench::toggleBookmarkedRemotePath(state, "/home/test"));
    QVERIFY(!ztermy::workbench::remotePathBookmarked(state, "/home/test"));

    for (std::size_t index = 0; index <= ztermy::workbench::maximumBookmarkedRemotePaths; ++index)
    {
        QVERIFY(ztermy::workbench::toggleBookmarkedRemotePath(state, "/path-" + std::to_string(index)));
    }
    QCOMPARE(state.bookmarkedRemotePaths.size(), ztermy::workbench::maximumBookmarkedRemotePaths);
    QCOMPARE(state.bookmarkedRemotePaths.front(), std::string("/path-32"));
    QVERIFY(!ztermy::workbench::remotePathBookmarked(state, "/path-0"));

    state.bookmarkedRemotePaths.back() = state.bookmarkedRemotePaths.front();
    QVERIFY(!ztermy::workbench::validProfileWorkspaceState(state));
}

void WorkspaceStateStoreTests::rejectsMalformedDuplicateAndInvalidState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schemaVersion":1,"profiles":[{"profileId":"same","lastRemotePath":"/","recentRemotePaths":[],"workbenchPage":"history","workbenchSide":"left","workbenchWidth":520,"composerHeight":132},{"profileId":"same","lastRemotePath":"/","recentRemotePaths":[],"workbenchPage":"history","workbenchSide":"left","workbenchWidth":520,"composerHeight":132}]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto duplicate = store.load();
    QVERIFY(!duplicate.has_value());
    QCOMPARE(duplicate.error(), ztermy::workbench::WorkspaceStateStoreError::InvalidDocument);

    ztermy::workbench::WorkspaceState invalid;
    invalid.profiles.push_back({.profileId = "profile", .lastRemotePath = "relative"});
    const auto saved = store.save(invalid);
    QVERIFY(!saved.has_value());
    QCOMPARE(saved.error(), ztermy::workbench::WorkspaceStateStoreError::InvalidDocument);
}

void WorkspaceStateStoreTests::rejectsMalformedTerminalWorkspaceTopology()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schemaVersion":6,"profiles":[],"collapsedHostSections":[],"activeTerminalWorkspaceId":"workspace","terminalWorkspaces":[{"id":"workspace","title":"Broken","rootNodeId":"root","activePaneId":"pane","nodes":[{"id":"root","kind":"split","restoreIntentId":"","firstChildId":"pane","secondChildId":"root","orientation":"horizontal","ratio":0.5},{"id":"pane","kind":"leaf","restoreIntentId":"intent","firstChildId":"","secondChildId":"","orientation":"horizontal","ratio":0.5}],"restoreIntents":[{"id":"intent","kind":"local","profileId":"","title":"PowerShell"}]}]})");
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto loaded = store.load();
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error(), ztermy::workbench::WorkspaceStateStoreError::InvalidDocument);
}

void WorkspaceStateStoreTests::recoversTheLastKnownGoodStateFromBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    const ztermy::workbench::WorkspaceStateStore store(path);

    ztermy::workbench::WorkspaceState first;
    first.profiles.push_back({.profileId = "host-a", .lastRemotePath = "/first"});
    ztermy::workbench::WorkspaceState second;
    second.profiles.push_back({.profileId = "host-a", .lastRemotePath = "/second"});
    QVERIFY(store.save(first).has_value());
    QVERIFY(store.save(second).has_value());
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));

    QFile corrupted(path);
    QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(corrupted.write("{corrupted"), qint64{10});
    corrupted.close();

    const auto recovered = store.load();
    QVERIFY(recovered.has_value());
    QCOMPARE(*recovered, first);
    QVERIFY(store.lastLoadRecoveredFromBackup());
}

void WorkspaceStateStoreTests::refusesToOverwriteANewerWorkspaceSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray futurePayload = R"({"schemaVersion":999,"profiles":[],"collapsedHostSections":[]})";
    QCOMPARE(file.write(futurePayload), futurePayload.size());
    file.close();

    const ztermy::workbench::WorkspaceStateStore store(path);
    const auto saved = store.save({});
    QVERIFY(!saved.has_value());
    QCOMPARE(saved.error(), ztermy::workbench::WorkspaceStateStoreError::UnsupportedVersion);

    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), futurePayload);
}

QTEST_GUILESS_MAIN(WorkspaceStateStoreTests)

#include "workspace_state_store_tests.moc"
