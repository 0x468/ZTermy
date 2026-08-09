#include "infrastructure/workbench/NoteStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class NoteStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void createsReadsRenamesSearchesAndRemovesNotes();
    void rejectsUnsafePathsInvalidUtf8AndOversizedWrites();
    void importsAndExportsPlainMarkdown();
    void boundsSearchResults();
};

void NoteStoreTests::createsReadsRenamesSearchesAndRemovesNotes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::workbench::NoteStore store(directory.filePath(QStringLiteral("notes")));

    QVERIFY(store.createFolder(QStringLiteral("运维")));
    QVERIFY(store.save(QStringLiteral("运维/检查.md"), QStringLiteral("# 检查\n\n运行 `df -h`。")));
    QCOMPARE(store.read(QStringLiteral("运维/检查.md")).value(), QStringLiteral("# 检查\n\n运行 `df -h`。"));

    const auto listed = store.entries();
    QVERIFY(listed);
    QCOMPARE(listed->size(), 2);
    QVERIFY(listed->front().folder);
    QCOMPARE(listed->back().path, QStringLiteral("运维/检查.md"));

    const auto results = store.search(QStringLiteral("DF -H"));
    QVERIFY(results);
    QCOMPARE(results->size(), 1);
    QCOMPARE(results->front().path, QStringLiteral("运维/检查.md"));
    QVERIFY(results->front().snippet.contains(QStringLiteral("df -h")));

    QVERIFY(store.renameEntry(QStringLiteral("运维/检查.md"), QStringLiteral("运维/磁盘检查.md")));
    QVERIFY(store.renameEntry(QStringLiteral("运维"), QStringLiteral("手册")));
    QVERIFY(store.read(QStringLiteral("手册/磁盘检查.md")));
    QVERIFY(store.removeEntry(QStringLiteral("手册")));
    QVERIFY(store.entries()->empty());
}

void NoteStoreTests::rejectsUnsafePathsInvalidUtf8AndOversizedWrites()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.filePath(QStringLiteral("notes"));
    ztermy::workbench::NoteStore store(root);

    QVERIFY(!store.save(QStringLiteral("../escape.md"), QStringLiteral("blocked")));
    QVERIFY(!store.save(QStringLiteral("C:/escape.md"), QStringLiteral("blocked")));
    QVERIFY(!store.save(QStringLiteral("CON.md"), QStringLiteral("blocked")));
    QVERIFY(!store.save(QStringLiteral("plain.txt"), QStringLiteral("blocked")));
    QVERIFY(!store.createFolder(QStringLiteral("a/").repeated(17) + QStringLiteral("leaf")));

    QVERIFY(store.save(QStringLiteral("stable.md"), QStringLiteral("original")));
    const QString oversized(ztermy::workbench::maximumNoteBytes + 1, QLatin1Char('x'));
    const auto oversizedResult = store.save(QStringLiteral("stable.md"), oversized);
    QVERIFY(!oversizedResult);
    QCOMPARE(oversizedResult.error(), ztermy::workbench::NoteStoreError::noteTooLarge);
    QCOMPARE(store.read(QStringLiteral("stable.md")).value(), QStringLiteral("original"));

    QFile invalidFile(directory.filePath(QStringLiteral("notes/invalid.md")));
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write(QByteArray::fromHex("C328")), 2);
    invalidFile.close();
    const auto invalid = store.read(QStringLiteral("invalid.md"));
    QVERIFY(!invalid);
    QCOMPARE(invalid.error(), ztermy::workbench::NoteStoreError::invalidUtf8);
}

void NoteStoreTests::importsAndExportsPlainMarkdown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::workbench::NoteStore store(directory.filePath(QStringLiteral("notes")));
    QVERIFY(store.createFolder(QStringLiteral("imports")));

    const QString sourcePath = directory.filePath(QStringLiteral("source.md"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray sourceContent = QStringLiteral("# Imported\n\n中文内容").toUtf8();
    QCOMPARE(source.write(sourceContent), sourceContent.size());
    source.close();

    const auto imported = store.importNote(sourcePath, QStringLiteral("imports"));
    QVERIFY(imported);
    QCOMPARE(*imported, QStringLiteral("imports/source.md"));
    const QString exportPath = directory.filePath(QStringLiteral("exported.md"));
    QVERIFY(store.exportNote(*imported, exportPath));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QCOMPARE(exported.readAll(), sourceContent);
}

void NoteStoreTests::boundsSearchResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::workbench::NoteStore store(directory.filePath(QStringLiteral("notes")));
    for (std::size_t index = 0; index < ztermy::workbench::maximumNoteSearchResults + 5; ++index)
    {
        QVERIFY(store.save(QStringLiteral("note-%1.md").arg(index), QStringLiteral("bounded needle %1").arg(index)));
    }
    const auto results = store.search(QStringLiteral("needle"));
    QVERIFY(results);
    QCOMPARE(results->size(), ztermy::workbench::maximumNoteSearchResults);
}

QTEST_GUILESS_MAIN(NoteStoreTests)

#include "note_store_tests.moc"
