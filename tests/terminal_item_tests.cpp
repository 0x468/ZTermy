#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalKeywordHighlighter.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
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

[[nodiscard]] std::shared_ptr<ztermy::terminal::TerminalSnapshot> snapshotAt(const quint16 column, const quint16 row)
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
    void exposesScrollbarAndRequestsAbsoluteScroll();
    void rendersStyledWideCellsAndCursorPixels();
    void rendersImeAcrossResizeAndShutdown();
    void highlightsWideAndCaseInsensitiveKeywords();
};

void TerminalItemTests::highlightsWideAndCaseInsensitiveKeywords()
{
    auto snapshot = snapshotAt(0, 0);
    snapshot->columns = 14;
    snapshot->rows = 1;
    snapshot->cells.assign(snapshot->columns, {});
    snapshot->cells[0].grapheme = {U'E'};
    snapshot->cells[1].grapheme = {U'R'};
    snapshot->cells[2].grapheme = {U'R'};
    snapshot->cells[3].grapheme = {U'O'};
    snapshot->cells[4].grapheme = {U'R'};
    snapshot->cells[6].grapheme = {U'错'};
    snapshot->cells[6].displayWidth = 2;
    snapshot->cells[7].displayWidth = 0;
    snapshot->cells[8].grapheme = {U'误'};
    snapshot->cells[8].displayWidth = 2;
    snapshot->cells[9].displayWidth = 0;
    snapshot->cells[11].grapheme = {U'A'};
    snapshot->cells[13].grapheme = {U'B'};

    const std::vector<ztermy::ui::TerminalKeywordRule> rules{
        {.id = QStringLiteral("error"),
         .pattern = QStringLiteral("error"),
         .foreground = {},
         .background = QColor(QStringLiteral("#d13438")),
         .enabled = true,
         .caseSensitive = false},
        {.id = QStringLiteral("overlap"),
         .pattern = QStringLiteral("err"),
         .foreground = QColor(QStringLiteral("#00ffff")),
         .background = {},
         .enabled = true,
         .caseSensitive = false},
        {.id = QStringLiteral("wide"),
         .pattern = QStringLiteral("错误"),
         .foreground = QColor(QStringLiteral("#111111")),
         .background = QColor(QStringLiteral("#ffcc00")),
         .enabled = true,
         .caseSensitive = true},
        {.id = QStringLiteral("no-blank-bridge"),
         .pattern = QStringLiteral("AB"),
         .foreground = {},
         .background = QColor(QStringLiteral("#00ff00")),
         .enabled = true,
         .caseSensitive = true},
    };
    const auto styles = ztermy::ui::highlightTerminalKeywords(*snapshot, rules);
    QCOMPARE(styles.size(), std::size_t{14});
    for (std::size_t column = 0; column < 5; ++column)
    {
        QCOMPARE(styles[column].background, QColor(QStringLiteral("#d13438")));
        QVERIFY(!styles[column].foreground.isValid());
    }
    for (std::size_t column = 6; column < 10; ++column)
    {
        QCOMPARE(styles[column].background, QColor(QStringLiteral("#ffcc00")));
    }
    QVERIFY(!styles[5].background.isValid());
    QVERIFY(!styles[10].background.isValid());
    QVERIFY(!styles[11].background.isValid());
    QVERIFY(!styles[13].background.isValid());
}

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
    QSignalSpy backgroundSpy(&item, &ztermy::ui::TerminalItem::backgroundOpacityChanged);

    item.setFontFamily(QStringLiteral("Cascadia Code"));
    item.setFontPixelSize(18);
    item.setLigaturesEnabled(false);
    item.setCursorPreference(QStringLiteral("bar"));
    item.setCursorBlink(false);
    item.setBackgroundOpacity(0.45);

    QCOMPARE(item.fontFamily(), QStringLiteral("Cascadia Code"));
    QCOMPARE(item.fontPixelSize(), 18);
    QVERIFY(!item.ligaturesEnabled());
    QCOMPARE(item.cursorPreference(), QStringLiteral("bar"));
    QVERIFY(!item.cursorBlink());
    QCOMPARE(item.backgroundOpacity(), 0.45);
    QCOMPARE(fontSpy.count(), 3);
    QCOMPARE(cursorSpy.count(), 2);
    QCOMPARE(backgroundSpy.count(), 1);

    item.setFontPixelSize(99);
    item.setCursorPreference(QStringLiteral("invalid"));
    item.setBackgroundOpacity(2.0);
    QCOMPARE(item.fontPixelSize(), 18);
    QCOMPARE(item.cursorPreference(), QStringLiteral("bar"));
    QCOMPARE(item.backgroundOpacity(), 1.0);
    QCOMPARE(backgroundSpy.count(), 2);
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

void TerminalItemTests::exposesScrollbarAndRequestsAbsoluteScroll()
{
    TestableTerminalItem item;
    auto snapshot = snapshotAt(0, 0);
    snapshot->scrollbar = {.total = 100, .offset = 40, .visible = 20};
    QSignalSpy scrollbarSpy(&item, &ztermy::ui::TerminalItem::scrollbarChanged);
    QSignalSpy scrollSpy(&item, &ztermy::ui::TerminalItem::scrollRequested);

    item.setSnapshot(snapshot);

    QVERIFY(item.scrollbarVisible());
    QCOMPARE(item.scrollbarPosition(), 0.5);
    QCOMPARE(item.scrollbarPageRatio(), 0.2);
    QCOMPARE(scrollbarSpy.count(), 1);

    item.scrollToFraction(0.75);
    QCOMPARE(scrollSpy.count(), 1);
    QCOMPARE(scrollSpy.constFirst().constFirst().toInt(), 20);
}

void TerminalItemTests::rendersStyledWideCellsAndCursorPixels()
{
    QQuickWindow window;
    auto *item = new TestableTerminalItem(window.contentItem());
    auto snapshot = std::make_shared<ztermy::terminal::TerminalSnapshot>();
    snapshot->columns = 8;
    snapshot->rows = 3;
    snapshot->defaultForeground = {.red = 230, .green = 232, .blue = 235};
    snapshot->defaultBackground = {.red = 5, .green = 7, .blue = 9};
    snapshot->cells.resize(static_cast<std::size_t>(snapshot->columns) * snapshot->rows);
    for (auto &cell : snapshot->cells)
    {
        cell.foreground = snapshot->defaultForeground;
        cell.background = snapshot->defaultBackground;
    }
    snapshot->cells[0].background = {.red = 180, .green = 20, .blue = 30};
    snapshot->cells[0].explicitBackground = true;
    snapshot->cells[2].selected = true;
    snapshot->cells[4].selected = true;
    snapshot->cells[4].displayWidth = 2;
    snapshot->cells[5].displayWidth = 0;
    snapshot->cells[8].grapheme = U"中";
    snapshot->cells[8].foreground = {.red = 240, .green = 40, .blue = 40};
    snapshot->cells[8].displayWidth = 2;
    snapshot->cells[9].displayWidth = 0;
    snapshot->cursor = {
        .column = 0,
        .row = 2,
        .width = 2,
        .style = ztermy::terminal::TerminalCursorStyle::block,
        .color = {.red = 20, .green = 230, .blue = 80},
        .visible = true,
    };

    item->setCursorBlink(false);
    item->setBackgroundOpacity(0.0);
    item->setSnapshot(snapshot);
    window.setColor(QColor(18, 52, 86));
    window.resize(640, 220);
    item->setSize(window.size());
    window.show();
    QTest::qWait(100);
    const QImage capture = window.grabWindow();
    QVERIFY(!capture.isNull());

    const QRectF cursorRect = item->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const qreal cellWidth = cursorRect.width() / 2.0;
    const qreal cellHeight = cursorRect.height();
    const qreal horizontalPadding = cursorRect.x();
    const qreal verticalPadding = cursorRect.y() - (2.0 * cellHeight);
    const qreal scale = capture.devicePixelRatio();
    const auto pixelAtCellCenter = [&](const int column, const int row) {
        const int x = qRound((horizontalPadding + ((static_cast<qreal>(column) + 0.5) * cellWidth)) * scale);
        const int y = qRound((verticalPadding + ((static_cast<qreal>(row) + 0.5) * cellHeight)) * scale);
        return capture.pixelColor(x, y);
    };

    const QColor styledBackground = pixelAtCellCenter(0, 0);
    QVERIFY(styledBackground.red() > 150);
    QVERIFY(styledBackground.green() < 50);
    const QColor transparentDefaultBackground = pixelAtCellCenter(7, 0);
    QVERIFY(qAbs(transparentDefaultBackground.red() - 18) <= 2);
    QVERIFY(qAbs(transparentDefaultBackground.green() - 52) <= 2);
    QVERIFY(qAbs(transparentDefaultBackground.blue() - 86) <= 2);
    const QColor selectionBackground = pixelAtCellCenter(2, 0);
    QVERIFY(selectionBackground.blue() > selectionBackground.red());
    QVERIFY(selectionBackground.blue() > selectionBackground.green());
    const QColor wideSelectionTail = pixelAtCellCenter(5, 0);
    QVERIFY(wideSelectionTail.blue() > wideSelectionTail.red());
    QVERIFY(wideSelectionTail.blue() > wideSelectionTail.green());

    const QColor cursorFirstCell = pixelAtCellCenter(0, 2);
    const QColor cursorSecondCell = pixelAtCellCenter(1, 2);
    QVERIFY(cursorFirstCell.green() > cursorFirstCell.red());
    QVERIFY(cursorFirstCell.green() > cursorFirstCell.blue());
    QVERIFY(cursorSecondCell.green() > cursorSecondCell.red());
    QVERIFY(cursorSecondCell.green() > cursorSecondCell.blue());

    int wideGlyphPixelsInSecondCell = 0;
    const int secondCellLeft = qRound((horizontalPadding + cellWidth) * scale);
    const int secondCellRight = qRound((horizontalPadding + (2.0 * cellWidth)) * scale);
    const int glyphTop = qRound((verticalPadding + cellHeight) * scale);
    const int glyphBottom = qRound((verticalPadding + (2.0 * cellHeight)) * scale);
    for (int y = glyphTop; y < glyphBottom; ++y)
    {
        for (int x = secondCellLeft; x < secondCellRight; ++x)
        {
            const QColor pixel = capture.pixelColor(x, y);
            if (pixel.red() > 100 && pixel.red() > (pixel.green() * 2) && pixel.red() > (pixel.blue() * 2))
            {
                ++wideGlyphPixelsInSecondCell;
            }
        }
    }
    QVERIFY(wideGlyphPixelsInSecondCell > 2);

    window.close();
    QCoreApplication::processEvents();
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
