#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <memory>

namespace
{

class TestableTerminalItem final : public ztermy::ui::TerminalItem
{
public:
    using TerminalItem::inputMethodEvent;
    using TerminalItem::inputMethodQuery;
    using TerminalItem::keyPressEvent;
    using TerminalItem::mouseMoveEvent;
    using TerminalItem::mousePressEvent;
    using TerminalItem::mouseReleaseEvent;
    using TerminalItem::TerminalItem;
    using TerminalItem::wheelEvent;

    QString clipboardTextFixture;

protected:
    [[nodiscard]] QString readClipboardText() const override { return clipboardTextFixture; }
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
    void appliesRendererPreferences();
    void routesCopyPasteAndTextKeys();
    void confirmsMultilinePaste();
    void selectsCellsAndCopiesOnMouseRelease();
    void accumulatesWheelDeltasIntoScrollRows();
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

void TerminalItemTests::appliesRendererPreferences()
{
    TestableTerminalItem item;
    QSignalSpy fontSpy(&item, &ztermy::ui::TerminalItem::fontChanged);
    QSignalSpy cursorSpy(&item, &ztermy::ui::TerminalItem::cursorAppearanceChanged);

    item.setFontFamily(QStringLiteral("Cascadia Code"));
    item.setFontPixelSize(18);
    item.setCursorPreference(QStringLiteral("bar"));
    item.setCursorBlink(false);

    QCOMPARE(item.fontFamily(), QStringLiteral("Cascadia Code"));
    QCOMPARE(item.fontPixelSize(), 18);
    QCOMPARE(item.cursorPreference(), QStringLiteral("bar"));
    QVERIFY(!item.cursorBlink());
    QCOMPARE(fontSpy.count(), 2);
    QCOMPARE(cursorSpy.count(), 2);

    item.setFontPixelSize(99);
    item.setCursorPreference(QStringLiteral("invalid"));
    QCOMPARE(item.fontPixelSize(), 18);
    QCOMPARE(item.cursorPreference(), QStringLiteral("bar"));
}

void TerminalItemTests::routesCopyPasteAndTextKeys()
{
    TestableTerminalItem item;
    QSignalSpy copySpy(&item, &ztermy::ui::TerminalItem::copyRequested);
    QSignalSpy pasteSpy(&item, &ztermy::ui::TerminalItem::pasteRequested);
    QSignalSpy confirmationSpy(&item, &ztermy::ui::TerminalItem::multilinePasteConfirmationRequested);
    QSignalSpy inputSpy(&item, &ztermy::ui::TerminalItem::inputGenerated);
    item.clipboardTextFixture = QStringLiteral("single-line paste");

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    item.keyPressEvent(&copyEvent);
    QCOMPARE(copySpy.count(), 1);
    QCOMPARE(inputSpy.count(), 0);
    QVERIFY(copyEvent.isAccepted());

    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    item.keyPressEvent(&pasteEvent);
    QCOMPARE(pasteSpy.count(), 1);
    QCOMPARE(pasteSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("single-line paste"));
    QCOMPARE(confirmationSpy.count(), 0);
    QCOMPARE(inputSpy.count(), 0);
    QVERIFY(pasteEvent.isAccepted());

    QKeyEvent textEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    item.keyPressEvent(&textEvent);
    QCOMPARE(inputSpy.count(), 1);
    QCOMPARE(inputSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("a"));
    QVERIFY(textEvent.isAccepted());
}

void TerminalItemTests::confirmsMultilinePaste()
{
    TestableTerminalItem item;
    QSignalSpy confirmationSpy(&item, &ztermy::ui::TerminalItem::multilinePasteConfirmationRequested);
    QSignalSpy pasteSpy(&item, &ztermy::ui::TerminalItem::pasteRequested);
    item.clipboardTextFixture = QStringLiteral("first\nsecond");

    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    item.keyPressEvent(&pasteEvent);

    QCOMPARE(confirmationSpy.count(), 1);
    QCOMPARE(confirmationSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(pasteSpy.count(), 0);

    item.resolveMultilinePaste(true);
    QCOMPARE(pasteSpy.count(), 1);
    QCOMPARE(pasteSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("first\nsecond"));
}

void TerminalItemTests::selectsCellsAndCopiesOnMouseRelease()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(0, 0));
    item.setSize(QSizeF{800, 480});
    item.setCopyOnSelect(true);
    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const auto cellCenter = [&origin](const int column, const int row) {
        return QPointF{origin.x() + ((static_cast<qreal>(column) + 0.5) * origin.width()),
                       origin.y() + ((static_cast<qreal>(row) + 0.5) * origin.height())};
    };

    QSignalSpy clearSpy(&item, &ztermy::ui::TerminalItem::clearSelectionRequested);
    QSignalSpy selectionSpy(&item, &ztermy::ui::TerminalItem::selectionRequested);
    QSignalSpy copySpy(&item, &ztermy::ui::TerminalItem::copyRequested);

    const QPointF start = cellCenter(2, 3);
    const QPointF end = cellCenter(7, 5);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QCOMPARE(clearSpy.count(), 1);
    QVERIFY(press.isAccepted());

    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    item.mouseMoveEvent(&move);
    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.at(0).at(0).toUInt(), 2U);
    QCOMPARE(selectionSpy.at(0).at(1).toUInt(), 3U);
    QCOMPARE(selectionSpy.at(0).at(2).toUInt(), 7U);
    QCOMPARE(selectionSpy.at(0).at(3).toUInt(), 5U);
    QVERIFY(!selectionSpy.at(0).at(4).toBool());
    QVERIFY(move.isAccepted());

    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    item.mouseReleaseEvent(&release);
    QCOMPARE(selectionSpy.count(), 2);
    QVERIFY(!selectionSpy.at(1).at(4).toBool());
    QCOMPARE(copySpy.count(), 1);
    QVERIFY(release.isAccepted());

    QMouseEvent rectangularPress(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton,
                                 Qt::AltModifier);
    item.mousePressEvent(&rectangularPress);
    QMouseEvent rectangularMove(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    item.mouseMoveEvent(&rectangularMove);
    QMouseEvent rectangularRelease(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton, Qt::NoButton,
                                   Qt::AltModifier);
    item.mouseReleaseEvent(&rectangularRelease);
    QCOMPARE(clearSpy.count(), 2);
    QCOMPARE(selectionSpy.count(), 4);
    QVERIFY(selectionSpy.at(2).at(4).toBool());
    QVERIFY(selectionSpy.at(3).at(4).toBool());
    QCOMPARE(copySpy.count(), 2);

    QMouseEvent clickPress(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
    item.mousePressEvent(&clickPress);
    QMouseEvent clickRelease(QEvent::MouseButtonRelease, start, start, start, Qt::LeftButton, Qt::NoButton,
                             Qt::NoModifier);
    item.mouseReleaseEvent(&clickRelease);
    QCOMPARE(clearSpy.count(), 3);
    QCOMPARE(selectionSpy.count(), 4);
    QCOMPARE(copySpy.count(), 2);
}

void TerminalItemTests::accumulatesWheelDeltasIntoScrollRows()
{
    TestableTerminalItem item;
    QSignalSpy scrollSpy(&item, &ztermy::ui::TerminalItem::scrollRequested);
    const auto sendWheel = [&item](const int angleDelta) {
        QWheelEvent event(QPointF{}, QPointF{}, QPoint{}, QPoint{0, angleDelta}, Qt::NoButton, Qt::NoModifier,
                          Qt::ScrollUpdate, false);
        item.wheelEvent(&event);
        QVERIFY(event.isAccepted());
    };

    sendWheel(60);
    QCOMPARE(scrollSpy.count(), 0);
    sendWheel(60);
    QCOMPARE(scrollSpy.count(), 1);
    QCOMPARE(scrollSpy.at(0).at(0).toInt(), -3);

    sendWheel(-240);
    QCOMPARE(scrollSpy.count(), 2);
    QCOMPARE(scrollSpy.at(1).at(0).toInt(), 6);
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
