#include "infrastructure/workbench/WorkspaceStateStore.h"

#include <QFile>
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
    void togglesAndBoundsSftpBookmarks();
    void rejectsMalformedDuplicateAndInvalidState();
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
        .workbenchWidth = 640.0,
        .composerHeight = 160.0,
    });
    expected.collapsedHostSections = {"recent", "group:Lab"};

    QVERIFY(store.save(expected).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, expected);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    QVERIFY(payload.contains("schemaVersion"));
    QVERIFY(!payload.contains("password"));
    QVERIFY(!payload.contains("secret"));
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

QTEST_GUILESS_MAIN(WorkspaceStateStoreTests)

#include "workspace_state_store_tests.moc"
