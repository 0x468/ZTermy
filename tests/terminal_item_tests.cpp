#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QInputMethodEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace
{

class TestableTerminalItem final : public ztermy::ui::TerminalItem
{
public:
    using TerminalItem::inputMethodEvent;
    using TerminalItem::inputMethodQuery;
    using TerminalItem::TerminalItem;
};

[[nodiscard]] ztermy::terminal::TerminalSnapshotPtr snapshotAt(const quint16 column, const quint16 row)
{
    auto snapshot = std::make_shared<ztermy::terminal::TerminalSnapshot>();
    snapshot->columns = 80;
    snapshot->rows = 24;
    snapshot->cursor = {
        .column = column,
        .row = row,
        .color = {.red = 255, .green = 255, .blue = 255},
        .visible = true,
    };
    snapshot->cells.resize(static_cast<std::size_t>(snapshot->columns) * snapshot->rows);
    return snapshot;
}

class TerminalItemTests final : public QObject
{
    Q_OBJECT

private slots:
    void positionsImeAtTerminalCursor();
    void tracksPreeditCursorWithoutSendingInput();
    void usesWideImeCursorAndShiftsSuffix();
    void commitsImeTextExactlyOnce();
    void rendersImeAcrossResizeAndShutdown();
};

void TerminalItemTests::positionsImeAtTerminalCursor()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(0, 0));
    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();

    item.setSnapshot(snapshotAt(3, 2));
    const QRectF moved = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();

    QCOMPARE(moved.x(), origin.x() + (origin.width() * 3.0));
    QCOMPARE(moved.y(), origin.y() + (origin.height() * 2.0));
}

void TerminalItemTests::tracksPreeditCursorWithoutSendingInput()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(4, 3));
    const QRectF baseCursor = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QSignalSpy inputSpy(&item, &ztermy::ui::TerminalItem::inputGenerated);

    QInputMethodEvent preedit(QStringLiteral("nihao"),
                              {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 2, 1, {})});
    item.inputMethodEvent(&preedit);

    QCOMPARE(inputSpy.count(), 0);
    const QRectF compositionCursor = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QVERIFY(compositionCursor.x() > baseCursor.x());
    QCOMPARE(compositionCursor.y(), baseCursor.y());
}

void TerminalItemTests::usesWideImeCursorAndShiftsSuffix()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(4, 3));
    const QRectF baseCursor = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();

    QInputMethodEvent preedit(QStringLiteral("中文"),
                              {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 0, 1, {})});
    item.inputMethodEvent(&preedit);
    const QRectF compositionCursor = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();

    QCOMPARE(compositionCursor.x(), baseCursor.x());
    QCOMPARE(compositionCursor.width(), baseCursor.width() * 2.0);
    QCOMPARE(ztermy::ui::shiftedTerminalColumn(4, 4, 3), 7);
    QCOMPARE(ztermy::ui::shiftedTerminalColumn(3, 4, 3), 3);
}

void TerminalItemTests::commitsImeTextExactlyOnce()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(1, 1));
    const QRectF baseCursor = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QSignalSpy inputSpy(&item, &ztermy::ui::TerminalItem::inputGenerated);

    QInputMethodEvent preedit(QStringLiteral("zhongwen"), {});
    item.inputMethodEvent(&preedit);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("中文"));
    item.inputMethodEvent(&commit);

    QCOMPARE(inputSpy.count(), 1);
    QCOMPARE(inputSpy.at(0).at(0).toByteArray(), QStringLiteral("中文").toUtf8());
    QCOMPARE(item.inputMethodQuery(Qt::ImCursorRectangle).toRectF(), baseCursor);
}

void TerminalItemTests::rendersImeAcrossResizeAndShutdown()
{
    QQuickWindow window;
    auto *item = new TestableTerminalItem(window.contentItem());
    item->setSnapshot(snapshotAt(4, 3));
    window.resize(1180, 760);
    item->setSize(window.size());
    window.show();
    QTest::qWait(50);

    for (int iteration = 0; iteration < 100; ++iteration)
    {
        const QSize size = iteration % 2 == 0 ? QSize(500, 360) : QSize(1180, 760);
        window.resize(size);
        item->setSize(size);

        const QString preeditText = iteration % 3 == 0 ? QStringLiteral("nihao") : QStringLiteral("中文nihao");
        const int preeditCursor = iteration % static_cast<int>(preeditText.size());
        QInputMethodEvent preedit(preeditText,
                                  {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, preeditCursor, 1, {})});
        item->inputMethodEvent(&preedit);
        QCoreApplication::processEvents();
    }

    window.close();
    QCoreApplication::processEvents();
}

} // namespace

QTEST_MAIN(TerminalItemTests)

#include "terminal_item_tests.moc"
