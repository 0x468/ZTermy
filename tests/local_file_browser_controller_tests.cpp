#include "application/workbench/LocalFileBrowserController.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class LocalFileBrowserControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void listsDirectoriesAndFilesWithoutBlocking();
    void reportsUnavailableDirectories();
};

void LocalFileBrowserControllerTests::listsDirectoriesAndFilesWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("folder"))));
    QFile file(directory.filePath(QStringLiteral("notes.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("ztermy"), qint64{6});
    file.close();

    ztermy::workbench::LocalFileBrowserController controller;
    controller.navigate(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2'000);
    QCOMPARE(controller.error(), QString{});
    QCOMPARE(controller.path(), QDir::cleanPath(directory.path()));
    QCOMPARE(controller.entries().size(), 2);
    QCOMPARE(controller.entries().front().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("folder"));
    QVERIFY(controller.entries().front().toMap().value(QStringLiteral("directory")).toBool());
}

void LocalFileBrowserControllerTests::reportsUnavailableDirectories()
{
    ztermy::workbench::LocalFileBrowserController controller;
    controller.navigate(QStringLiteral("Z:/ztermy-path-that-does-not-exist"));
    QVERIFY(!controller.error().isEmpty());
}

QTEST_GUILESS_MAIN(LocalFileBrowserControllerTests)

#include "local_file_browser_controller_tests.moc"
