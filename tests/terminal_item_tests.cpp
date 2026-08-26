#include "platform/windows/WindowsTerminalInput.h"
#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalKeywordHighlighter.h"
#include "ui/terminal/TerminalRenderMetrics.h"
#include "ui/terminal/TerminalRowReuseAnalysis.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QColor>
#include <QDropEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

namespace
{

class TestableTerminalItem final : public ztermy::ui::TerminalItem
{
public:
    using TerminalItem::dropEvent;
    using TerminalItem::hoverLeaveEvent;
    using TerminalItem::hoverMoveEvent;
    using TerminalItem::inputMethodEvent;
    using TerminalItem::inputMethodQuery;
    using TerminalItem::keyPressEvent;
    using TerminalItem::keyReleaseEvent;
    using TerminalItem::mouseDoubleClickEvent;
    using TerminalItem::mouseMoveEvent;
    using TerminalItem::mousePressEvent;
    using TerminalItem::mouseReleaseEvent;
    using TerminalItem::mouseUngrabEvent;
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
    void mapsWindowsPhysicalKeys();
    void confirmsMultilinePaste();
    void selectsCellsAndCopiesOnMouseRelease();
    void supportsClassicClipboardAliasesAndContextActions();
    void selectsWordsOnDoubleClick();
    void selectsLinesOnTripleClickAndExtendsWithShift();
    void autoscrollsSelectionNearViewportEdges();
    void cancelsTrackedSelectionWhenMouseGrabIsLost();
    void reflectsSelectionStateFromSnapshots();
    void hoversAndActivatesLinksAheadOfMouseTracking();
    void labelsQuickSelectTargetsWithoutPrefixes();
    void quickSelectInsertsOrOpensExactlyOnce();
    void routesCopyModeKeysWithoutSendingTerminalInput();
    void routesDroppedFilesWithoutPastingLocalPaths();
    void accumulatesWheelDeltasIntoScrollRows();
    void routesTrackedMouseAndWheelToTerminal();
    void exposesScrollbarAndRequestsAbsoluteScroll();
    void rendersStyledWideCellsAndCursorPixels();
    void keepsBaseTextureDuringCursorBlink();
    void rendersImeAcrossResizeAndShutdown();
    void highlightsWideAndCaseInsensitiveKeywords();
    void recordsOptInRenderMetrics();
    void analyzesTerminalRowReuse();
};

void TerminalItemTests::labelsQuickSelectTargetsWithoutPrefixes()
{
    auto snapshot = snapshotAt(0, 0);
    snapshot->columns = 80;
    snapshot->rows = 2;
    snapshot->cells.assign(160, {});
    for (quint16 column = 0; column < 30; ++column)
    {
        snapshot->hyperlinks.push_back({.uri = "https://example.test/" + std::to_string(column),
                                        .kind = ztermy::terminal::TerminalHyperlinkKind::explicitOsc8});
        snapshot->cell(static_cast<quint16>(column * 2), 0).grapheme = U"x";
        snapshot->cell(static_cast<quint16>(column * 2), 0).hyperlinkId = column + 1;
    }
    const auto targets = ztermy::ui::quickSelectTargets(*snapshot);
    QCOMPARE(targets.size(), std::size_t{30});
    for (const auto &target : targets)
    {
        QCOMPARE(target.label.size(), qsizetype{2});
        for (const auto &other : targets)
        {
            if (&target != &other)
            {
                QVERIFY(!other.label.startsWith(target.label));
            }
        }
    }
}

void TerminalItemTests::quickSelectInsertsOrOpensExactlyOnce()
{
    TestableTerminalItem item;
    auto snapshot = snapshotAt(0, 0);
    snapshot->hyperlinks.push_back(
        {.uri = "https://example.test", .kind = ztermy::terminal::TerminalHyperlinkKind::explicitOsc8});
    snapshot->cell(0, 0).grapheme = U"x";
    snapshot->cell(0, 0).hyperlinkId = 1;
    item.setSnapshot(snapshot);

    QSignalSpy paste(&item, &ztermy::ui::TerminalItem::pasteRequested);
    item.startQuickSelect();
    QVERIFY(item.quickSelectActive());
    QKeyEvent shiftLabel(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A"));
    item.keyPressEvent(&shiftLabel);
    QVERIFY(!item.quickSelectActive());
    QCOMPARE(paste.count(), 1);
    QCOMPARE(paste.takeFirst().at(0).toByteArray(), QByteArray("x"));

    QSignalSpy opened(&item, &ztermy::ui::TerminalItem::linkActivated);
    item.startQuickSelect();
    QKeyEvent controlLabel(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    item.keyPressEvent(&controlLabel);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().at(0).toString(), QStringLiteral("https://example.test"));
}

void TerminalItemTests::routesCopyModeKeysWithoutSendingTerminalInput()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(5, 2));
    std::vector<ztermy::terminal::TerminalCopyModeAction> actions;
    QObject::connect(&item, &ztermy::ui::TerminalItem::copyModeActionRequested, &item,
                     [&actions](const ztermy::terminal::TerminalCopyModeAction &action) {
                         actions.push_back(action);
                     });
    QSignalSpy terminalInput(&item, &ztermy::ui::TerminalItem::keyEventGenerated);
    QSignalSpy copy(&item, &ztermy::ui::TerminalItem::copyRequested);

    item.startCopyMode();
    QVERIFY(item.copyModeActive());
    QCOMPARE(actions.size(), std::size_t{1});
    QCOMPARE(actions.back().type, ztermy::terminal::TerminalCopyModeActionType::begin);

    QKeyEvent extendLeft(QEvent::KeyPress, Qt::Key_Left, Qt::ShiftModifier);
    item.keyPressEvent(&extendLeft);
    QCOMPARE(actions.back().type, ztermy::terminal::TerminalCopyModeActionType::move);
    QCOMPARE(actions.back().motion, ztermy::terminal::TerminalCopyModeMotion::left);
    QVERIFY(actions.back().extend);

    QKeyEvent rectangle(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    item.keyPressEvent(&rectangle);
    QCOMPARE(actions.back().type, ztermy::terminal::TerminalCopyModeActionType::selectRectangle);

    QKeyEvent yank(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier, QStringLiteral("y"));
    item.keyPressEvent(&yank);
    QVERIFY(!item.copyModeActive());
    QCOMPARE(copy.count(), 1);
    QCOMPARE(actions.back().type, ztermy::terminal::TerminalCopyModeActionType::cancel);
    QCOMPARE(terminalInput.count(), 0);
}

void TerminalItemTests::routesDroppedFilesWithoutPastingLocalPaths()
{
    TestableTerminalItem item;
    QSignalSpy files(&item, &ztermy::ui::TerminalItem::localFilesDropped);
    QSignalSpy paste(&item, &ztermy::ui::TerminalItem::pasteRequested);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/one file.txt")),
                  QUrl::fromLocalFile(QStringLiteral("D:/two.txt"))});
    QDropEvent event({10.0, 10.0}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    item.dropEvent(&event);

    QCOMPARE(files.count(), 1);
    QCOMPARE(files.takeFirst().at(0).toStringList(),
             QStringList({QStringLiteral("C:/Temp/one file.txt"), QStringLiteral("D:/two.txt")}));
    QCOMPARE(paste.count(), 0);
    QVERIFY(event.isAccepted());
}

void TerminalItemTests::hoversAndActivatesLinksAheadOfMouseTracking()
{
    TestableTerminalItem item;
    item.setSize({800, 480});
    auto snapshot = snapshotAt(0, 0);
    snapshot->mouseTrackingActive = true;
    snapshot->hyperlinks.push_back(
        {.uri = "https://example.test", .kind = ztermy::terminal::TerminalHyperlinkKind::explicitOsc8});
    snapshot->cell(0, 0).grapheme = U"x";
    snapshot->cell(0, 0).hyperlinkId = 1;
    item.setSnapshot(snapshot);

    const QPointF point{17.0, 15.0};
    QHoverEvent hover(QEvent::HoverMove, point, point, point, Qt::NoModifier);
    item.hoverMoveEvent(&hover);
    QCOMPARE(item.hoveredLink(), QStringLiteral("https://example.test"));

    QSignalSpy activated(&item, &ztermy::ui::TerminalItem::linkActivated);
    QSignalSpy mouseEvents(&item, &ztermy::ui::TerminalItem::mouseEventGenerated);
    QMouseEvent press(QEvent::MouseButtonPress, point, point, point, Qt::LeftButton, Qt::LeftButton,
                      Qt::ControlModifier);
    item.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, point, Qt::LeftButton, Qt::NoButton,
                        Qt::ControlModifier);
    item.mouseReleaseEvent(&release);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.takeFirst().at(0).toString(), QStringLiteral("https://example.test"));
    QCOMPARE(mouseEvents.count(), 0);

    QHoverEvent leave(QEvent::HoverLeave, point, point, point, Qt::NoModifier);
    item.hoverLeaveEvent(&leave);
    QVERIFY(item.hoveredLink().isEmpty());
}

void TerminalItemTests::analyzesTerminalRowReuse()
{
    auto previous = snapshotAt(0, 0);
    previous->columns = 3;
    previous->rows = 4;
    previous->cells.assign(12, {});
    for (quint16 row = 0; row < previous->rows; ++row)
    {
        previous->cell(0, row).grapheme = {static_cast<char32_t>(U'A' + row)};
    }

    const auto &identical = *previous;
    auto analysis = ztermy::ui::analyzeTerminalRowReuse(*previous, identical);
    QCOMPARE(analysis.rowShift, 0);
    QCOMPARE(analysis.reusableRows, std::size_t{4});
    QCOMPARE(analysis.repaintRows(), std::size_t{0});

    auto scrolled = *previous;
    for (quint16 row = 0; row + 1 < scrolled.rows; ++row)
    {
        for (quint16 column = 0; column < scrolled.columns; ++column)
        {
            scrolled.cell(column, row) = previous->cell(column, row + 1);
        }
    }
    scrolled.cell(0, 3).grapheme = {U'Z'};
    analysis = ztermy::ui::analyzeTerminalRowReuse(*previous, scrolled);
    QCOMPARE(analysis.rowShift, 1);
    QCOMPARE(analysis.reusableRows, std::size_t{3});
    QCOMPARE(analysis.repaintRows(), std::size_t{1});
    QVERIFY(analysis.shifted());

    auto changed = *previous;
    changed.cell(0, 2).bold = true;
    analysis = ztermy::ui::analyzeTerminalRowReuse(*previous, changed);
    QCOMPARE(analysis.rowShift, 0);
    QCOMPARE(analysis.reusableRows, std::size_t{3});

    changed.defaultBackground.red = 1;
    analysis = ztermy::ui::analyzeTerminalRowReuse(*previous, changed);
    QCOMPARE(analysis.reusableRows, std::size_t{0});
    QCOMPARE(analysis.repaintRows(), std::size_t{4});
}

void TerminalItemTests::recordsOptInRenderMetrics()
{
    ztermy::ui::TerminalRenderMetrics metrics;
    metrics.recordFrame(std::chrono::microseconds{1500}, std::chrono::microseconds{2500}, 100,
                        ztermy::terminal::TerminalDamageKind::partial, 3);
    QCOMPARE(metrics.snapshot().renderedFrames, std::uint64_t{0});

    metrics.setEnabled(true);
    metrics.recordSnapshot(ztermy::terminal::TerminalDamageKind::partial, 2);
    metrics.recordCursorInvalidation();
    metrics.recordRowReuse(24, 20, true);
    metrics.recordPaintPhases({
        .imagePreparation = std::chrono::microseconds{100},
        .snapshotPreparation = std::chrono::microseconds{200},
        .backgroundPaint = std::chrono::microseconds{300},
        .textPaint = std::chrono::microseconds{700},
        .overlayPaint = std::chrono::microseconds{50},
    });
    metrics.recordFrame(std::chrono::microseconds{1500}, std::chrono::microseconds{2500}, 100,
                        ztermy::terminal::TerminalDamageKind::partial, 3);
    const ztermy::ui::TerminalRenderMetricsSnapshot snapshot = metrics.snapshot();
    QCOMPARE(snapshot.paintLatency.count, std::uint64_t{1});
    QCOMPARE(snapshot.paintLatency.p95UpperBoundMicroseconds, std::uint64_t{2000});
    QCOMPARE(snapshot.textureLatency.p95UpperBoundMicroseconds, std::uint64_t{4000});
    QCOMPARE(snapshot.renderedFrames, std::uint64_t{1});
    QCOMPARE(snapshot.partialFrames, std::uint64_t{1});
    QCOMPARE(snapshot.renderedDamagedRows, std::uint64_t{3});
    QCOMPARE(snapshot.snapshotUpdates, std::uint64_t{1});
    QCOMPARE(snapshot.partialSnapshotUpdates, std::uint64_t{1});
    QCOMPARE(snapshot.snapshotDamagedRows, std::uint64_t{2});
    QCOMPARE(snapshot.cursorInvalidations, std::uint64_t{1});
    QCOMPARE(snapshot.uploadedBytes, std::uint64_t{400});
    QCOMPARE(snapshot.maximumFramePixels, std::uint64_t{100});
    QCOMPARE(snapshot.rowReuseAnalysisFrames, std::uint64_t{1});
    QCOMPARE(snapshot.rowReuseCandidateRows, std::uint64_t{24});
    QCOMPARE(snapshot.rowReuseReusableRows, std::uint64_t{20});
    QCOMPARE(snapshot.rowReuseRepaintRows, std::uint64_t{4});
    QCOMPARE(snapshot.rowReuseShiftedFrames, std::uint64_t{1});
    QCOMPARE(snapshot.imagePreparationLatency.count, std::uint64_t{1});
    QCOMPARE(snapshot.snapshotPreparationLatency.count, std::uint64_t{1});
    QCOMPARE(snapshot.backgroundPaintLatency.count, std::uint64_t{1});
    QCOMPARE(snapshot.textPaintLatency.count, std::uint64_t{1});
    QCOMPARE(snapshot.overlayPaintLatency.count, std::uint64_t{1});

    metrics.reset();
    QCOMPARE(metrics.snapshot().renderedFrames, std::uint64_t{0});
}

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
    std::vector<ztermy::terminal::TerminalKeyEvent> keyEvents;
    QObject::connect(&item, &ztermy::ui::TerminalItem::keyEventGenerated, &item,
                     [&keyEvents](const ztermy::terminal::TerminalKeyEvent &event) {
                         keyEvents.push_back(event);
                     });
    item.clipboardTextFixture = QStringLiteral("single-line paste");

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_Insert, Qt::ControlModifier);
    item.keyPressEvent(&copyEvent);
    QCOMPARE(copySpy.count(), 1);
    QVERIFY(keyEvents.empty());
    QVERIFY(copyEvent.isAccepted());

    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_Insert, Qt::ShiftModifier);
    item.keyPressEvent(&pasteEvent);
    QCOMPARE(pasteSpy.count(), 1);
    QCOMPARE(pasteSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("single-line paste"));
    QCOMPARE(confirmationSpy.count(), 0);
    QVERIFY(keyEvents.empty());
    QVERIFY(pasteEvent.isAccepted());

    QKeyEvent textEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    item.keyPressEvent(&textEvent);
    QCOMPARE(keyEvents.size(), std::size_t{1});
    QCOMPARE(keyEvents.front().key, ztermy::terminal::TerminalKey::keyA);
    QCOMPARE(keyEvents.front().text, std::string("a"));
    QVERIFY(textEvent.isAccepted());
}

void TerminalItemTests::mapsWindowsPhysicalKeys()
{
    QKeyEvent layoutEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, 0x10, 'A', 0, QStringLiteral("a"));
    const auto mapped =
        ztermy::platform::windows::terminalKeyEvent(layoutEvent, ztermy::terminal::TerminalKeyAction::press);
    QCOMPARE(mapped.key, ztermy::terminal::TerminalKey::keyQ);
    QCOMPARE(mapped.text, std::string("a"));

    QKeyEvent keypadEnter(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier, 0x1C, 0x0D, 0);
    const auto keypad =
        ztermy::platform::windows::terminalKeyEvent(keypadEnter, ztermy::terminal::TerminalKeyAction::press);
    QCOMPARE(keypad.key, ztermy::terminal::TerminalKey::numpadEnter);
}

void TerminalItemTests::supportsClassicClipboardAliasesAndContextActions()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(0, 0));
    item.setSize(QSizeF{800, 480});
    item.clipboardTextFixture = QStringLiteral("classic paste");
    QSignalSpy copySpy(&item, &ztermy::ui::TerminalItem::copyRequested);
    QSignalSpy pasteSpy(&item, &ztermy::ui::TerminalItem::pasteRequested);
    std::vector<ztermy::terminal::TerminalKeyEvent> keyEvents;
    QObject::connect(&item, &ztermy::ui::TerminalItem::keyEventGenerated, &item,
                     [&keyEvents](const ztermy::terminal::TerminalKeyEvent &event) {
                         keyEvents.push_back(event);
                     });
    QSignalSpy contextSpy(&item, &ztermy::ui::TerminalItem::contextMenuRequested);

    item.selectVisibleTerminal();
    QVERIFY(item.hasSelection());
    QKeyEvent controlDown(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    item.keyPressEvent(&controlDown);
    QVERIFY(item.hasSelection());
    keyEvents.clear();
    QKeyEvent ctrlInsert(QEvent::KeyPress, Qt::Key_Insert, Qt::ControlModifier);
    item.keyPressEvent(&ctrlInsert);
    QCOMPARE(copySpy.count(), 1);

    QKeyEvent conditionalCopy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    item.keyPressEvent(&conditionalCopy);
    QCOMPARE(copySpy.count(), 2);
    QVERIFY(keyEvents.empty());
    QKeyEvent controlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    item.keyReleaseEvent(&controlUp);
    keyEvents.clear();

    item.clearSelection();
    QKeyEvent interrupt(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    item.keyPressEvent(&interrupt);
    QCOMPARE(keyEvents.size(), std::size_t{1});
    QCOMPARE(keyEvents.front().key, ztermy::terminal::TerminalKey::keyC);
    QVERIFY((keyEvents.front().modifiers & ztermy::terminal::terminalModifierControl) != 0);

    QKeyEvent shiftInsert(QEvent::KeyPress, Qt::Key_Insert, Qt::ShiftModifier);
    item.keyPressEvent(&shiftInsert);
    QCOMPARE(pasteSpy.count(), 1);
    QCOMPARE(pasteSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("classic paste"));

    const QPointF point{40, 40};
    QMouseEvent menuEvent(QEvent::MouseButtonPress, point, point, point, Qt::RightButton, Qt::RightButton,
                          Qt::NoModifier);
    item.mousePressEvent(&menuEvent);
    QCOMPARE(contextSpy.count(), 1);

    item.setRightClickBehavior(QStringLiteral("paste"));
    QMouseEvent pasteEvent(QEvent::MouseButtonPress, point, point, point, Qt::RightButton, Qt::RightButton,
                           Qt::NoModifier);
    item.mousePressEvent(&pasteEvent);
    QCOMPARE(pasteSpy.count(), 2);

    QMouseEvent forcedMenu(QEvent::MouseButtonPress, point, point, point, Qt::RightButton, Qt::RightButton,
                           Qt::ShiftModifier);
    item.mousePressEvent(&forcedMenu);
    QCOMPARE(contextSpy.count(), 2);

    QMouseEvent disabledMiddle(QEvent::MouseButtonPress, point, point, point, Qt::MiddleButton, Qt::MiddleButton,
                               Qt::NoModifier);
    item.mousePressEvent(&disabledMiddle);
    QCOMPARE(pasteSpy.count(), 2);
    QCOMPARE(contextSpy.count(), 2);

    item.setMiddleClickBehavior(QStringLiteral("paste"));
    QMouseEvent middlePaste(QEvent::MouseButtonPress, point, point, point, Qt::MiddleButton, Qt::MiddleButton,
                            Qt::NoModifier);
    item.mousePressEvent(&middlePaste);
    QCOMPARE(pasteSpy.count(), 3);

    item.setMiddleClickBehavior(QStringLiteral("context-menu"));
    QMouseEvent middleMenu(QEvent::MouseButtonPress, point, point, point, Qt::MiddleButton, Qt::MiddleButton,
                           Qt::NoModifier);
    item.mousePressEvent(&middleMenu);
    QCOMPARE(contextSpy.count(), 3);
}

void TerminalItemTests::selectsWordsOnDoubleClick()
{
    TestableTerminalItem item;
    auto snapshot = snapshotAt(0, 0);
    const QString word = QStringLiteral("hello-world");
    for (qsizetype index = 0; index < word.size(); ++index)
    {
        snapshot->cell(static_cast<quint16>(index + 2), 1).grapheme = {word.at(index).unicode()};
    }
    item.setSnapshot(snapshot);
    item.setWordDelimiters(QStringLiteral(" -"));
    item.setSize(QSizeF{800, 480});
    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const QPointF point{origin.x() + (4.5 * origin.width()), origin.y() + (1.5 * origin.height())};
    QSignalSpy selectionSpy(&item, &ztermy::ui::TerminalItem::selectionRequested);

    QMouseEvent event(QEvent::MouseButtonDblClick, point, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    event.setTimestamp(100);
    item.mouseDoubleClickEvent(&event);

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.at(0).at(0).toUInt(), 2U);
    QCOMPARE(selectionSpy.at(0).at(1).toUInt(), 1U);
    QCOMPARE(selectionSpy.at(0).at(2).toUInt(), 6U);
    QCOMPARE(selectionSpy.at(0).at(3).toUInt(), 1U);
    QVERIFY(item.hasSelection());
}

void TerminalItemTests::selectsLinesOnTripleClickAndExtendsWithShift()
{
    TestableTerminalItem item;
    auto snapshot = snapshotAt(0, 0);
    const QString word = QStringLiteral("hello");
    for (qsizetype index = 0; index < word.size(); ++index)
    {
        snapshot->cell(static_cast<quint16>(index + 2), 1).grapheme = {word.at(index).unicode()};
    }
    item.setSnapshot(snapshot);
    item.setSize(QSizeF{800, 480});
    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const auto cellCenter = [&origin](const int column, const int row) {
        return QPointF{origin.x() + ((static_cast<qreal>(column) + 0.5) * origin.width()),
                       origin.y() + ((static_cast<qreal>(row) + 0.5) * origin.height())};
    };
    const QPointF wordPoint = cellCenter(4, 1);
    QSignalSpy selectionSpy(&item, &ztermy::ui::TerminalItem::selectionRequested);

    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, wordPoint, wordPoint, wordPoint, Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    doubleClick.setTimestamp(100);
    item.mouseDoubleClickEvent(&doubleClick);

    QMouseEvent thirdClick(QEvent::MouseButtonPress, wordPoint, wordPoint, wordPoint, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
    thirdClick.setTimestamp(200);
    item.mousePressEvent(&thirdClick);
    QCOMPARE(selectionSpy.count(), 2);
    QCOMPARE(selectionSpy.at(0).at(0).toUInt(), 2U);
    QCOMPARE(selectionSpy.at(0).at(2).toUInt(), 6U);
    QCOMPARE(selectionSpy.at(1).at(0).toUInt(), 0U);
    QCOMPARE(selectionSpy.at(1).at(2).toUInt(), 79U);

    const QPointF extensionPoint = cellCenter(10, 3);
    QKeyEvent shiftDown(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    item.keyPressEvent(&shiftDown);
    QVERIFY(item.hasSelection());
    QMouseEvent shiftClick(QEvent::MouseButtonPress, extensionPoint, extensionPoint, extensionPoint, Qt::LeftButton,
                           Qt::LeftButton, Qt::ShiftModifier);
    shiftClick.setTimestamp(1000);
    item.mousePressEvent(&shiftClick);
    QCOMPARE(selectionSpy.count(), 3);
    QCOMPARE(selectionSpy.at(2).at(0).toUInt(), 0U);
    QCOMPARE(selectionSpy.at(2).at(1).toUInt(), 1U);
    QCOMPARE(selectionSpy.at(2).at(2).toUInt(), 10U);
    QCOMPARE(selectionSpy.at(2).at(3).toUInt(), 3U);

    const QPointF dragPoint = cellCenter(15, 4);
    QMouseEvent shiftDrag(QEvent::MouseMove, dragPoint, dragPoint, dragPoint, Qt::NoButton, Qt::LeftButton,
                          Qt::ShiftModifier);
    item.mouseMoveEvent(&shiftDrag);
    QCOMPARE(selectionSpy.count(), 4);
    QCOMPARE(selectionSpy.at(3).at(0).toUInt(), 0U);
    QCOMPARE(selectionSpy.at(3).at(1).toUInt(), 1U);
    QCOMPARE(selectionSpy.at(3).at(2).toUInt(), 15U);
    QCOMPARE(selectionSpy.at(3).at(3).toUInt(), 4U);

    QMouseEvent shiftRelease(QEvent::MouseButtonRelease, dragPoint, dragPoint, dragPoint, Qt::LeftButton, Qt::NoButton,
                             Qt::ShiftModifier);
    item.mouseReleaseEvent(&shiftRelease);
    QCOMPARE(selectionSpy.count(), 5);
    QCOMPARE(selectionSpy.at(4).at(2).toUInt(), 15U);
    QCOMPARE(selectionSpy.at(4).at(3).toUInt(), 4U);
}

void TerminalItemTests::confirmsMultilinePaste()
{
    TestableTerminalItem item;
    QSignalSpy confirmationSpy(&item, &ztermy::ui::TerminalItem::multilinePasteConfirmationRequested);
    QSignalSpy pasteSpy(&item, &ztermy::ui::TerminalItem::pasteRequested);
    item.clipboardTextFixture = QStringLiteral("first\nsecond");

    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_Insert, Qt::ShiftModifier);
    item.keyPressEvent(&pasteEvent);

    QCOMPARE(confirmationSpy.count(), 1);
    QVERIFY(item.multilinePastePending());
    QCOMPARE(confirmationSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(pasteSpy.count(), 0);

    item.resolveMultilinePaste(true);
    QVERIFY(!item.multilinePastePending());
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

    std::vector<ztermy::terminal::TerminalSelectionGesture> gestures;
    connect(&item, &ztermy::ui::TerminalItem::selectionGestureRequested, &item, [&gestures](const auto &gesture) {
        gestures.push_back(gesture);
    });
    QSignalSpy copySpy(&item, &ztermy::ui::TerminalItem::copyRequested);
    QSignalSpy selectionActionSpy(&item, &ztermy::ui::TerminalItem::selectionActionChanged);

    const QPointF start = cellCenter(2, 3);
    const QPointF end = cellCenter(7, 5);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QCOMPARE(gestures.size(), std::size_t{1});
    QCOMPARE(gestures.back().type, ztermy::terminal::TerminalSelectionGestureType::press);
    QCOMPARE(gestures.back().point.column, 2U);
    QCOMPARE(gestures.back().point.row, 3U);
    QVERIFY(press.isAccepted());

    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    item.mouseMoveEvent(&move);
    QCOMPARE(gestures.size(), std::size_t{2});
    QCOMPARE(gestures.back().type, ztermy::terminal::TerminalSelectionGestureType::drag);
    QCOMPARE(gestures.back().point.column, 7U);
    QCOMPARE(gestures.back().point.row, 5U);
    QVERIFY(!gestures.back().rectangular);
    QVERIFY(move.isAccepted());

    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    item.mouseReleaseEvent(&release);
    QCOMPARE(gestures.size(), std::size_t{4});
    QCOMPARE(gestures.at(2).type, ztermy::terminal::TerminalSelectionGestureType::drag);
    QCOMPARE(gestures.at(3).type, ztermy::terminal::TerminalSelectionGestureType::release);
    QCOMPARE(copySpy.count(), 1);
    QVERIFY(item.selectionActionVisible());
    QCOMPARE(item.selectionActionPosition(), end);
    QCOMPARE(selectionActionSpy.count(), 1);
    QVERIFY(release.isAccepted());

    QMouseEvent rectangularPress(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton,
                                 Qt::AltModifier);
    item.mousePressEvent(&rectangularPress);
    QVERIFY(!item.selectionActionVisible());
    QMouseEvent rectangularMove(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    item.mouseMoveEvent(&rectangularMove);
    QMouseEvent rectangularRelease(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton, Qt::NoButton,
                                   Qt::AltModifier);
    item.mouseReleaseEvent(&rectangularRelease);
    QCOMPARE(gestures.size(), std::size_t{8});
    QCOMPARE(gestures.at(4).type, ztermy::terminal::TerminalSelectionGestureType::press);
    QVERIFY(gestures.at(5).rectangular);
    QVERIFY(gestures.at(6).rectangular);
    QCOMPARE(gestures.at(7).type, ztermy::terminal::TerminalSelectionGestureType::release);
    QCOMPARE(copySpy.count(), 2);
    QVERIFY(item.selectionActionVisible());

    item.dismissSelectionAction();
    QVERIFY(!item.selectionActionVisible());

    QMouseEvent clickPress(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
    item.mousePressEvent(&clickPress);
    QMouseEvent clickRelease(QEvent::MouseButtonRelease, start, start, start, Qt::LeftButton, Qt::NoButton,
                             Qt::NoModifier);
    item.mouseReleaseEvent(&clickRelease);
    QCOMPARE(gestures.size(), std::size_t{10});
    QCOMPARE(gestures.at(8).type, ztermy::terminal::TerminalSelectionGestureType::press);
    QCOMPARE(gestures.at(9).type, ztermy::terminal::TerminalSelectionGestureType::release);
    QCOMPARE(copySpy.count(), 2);
    QVERIFY(!item.selectionActionVisible());
}

void TerminalItemTests::autoscrollsSelectionNearViewportEdges()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(0, 0));
    item.setSize(QSizeF{800, 480});
    std::vector<ztermy::terminal::TerminalSelectionGesture> gestures;
    connect(&item, &ztermy::ui::TerminalItem::selectionGestureRequested, &item, [&gestures](const auto &gesture) {
        gestures.push_back(gesture);
    });

    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const QPointF start{origin.x() + (2.5 * origin.width()), origin.y() + (3.5 * origin.height())};
    const QPointF belowViewport{start.x(), item.height() + (2.0 * origin.height())};
    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, belowViewport, belowViewport, belowViewport, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    item.mouseMoveEvent(&move);

    const auto edgeDrag = std::ranges::find_if(gestures, [](const auto &gesture) {
        return gesture.type == ztermy::terminal::TerminalSelectionGestureType::drag;
    });
    QVERIFY(edgeDrag != gestures.end());
    QCOMPARE(edgeDrag->positionY, item.height());

    QTest::qWait(160);
    bool observedAutoscroll = false;
    for (const auto &gesture : gestures)
    {
        if (gesture.type == ztermy::terminal::TerminalSelectionGestureType::autoscrollTick && gesture.scrollRows > 0)
        {
            observedAutoscroll = true;
            break;
        }
    }
    QVERIFY(observedAutoscroll);

    QMouseEvent release(QEvent::MouseButtonRelease, belowViewport, belowViewport, belowViewport, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    item.mouseReleaseEvent(&release);
    const std::size_t releasedCount = gestures.size();
    QTest::qWait(70);
    QCOMPARE(gestures.size(), releasedCount);
}

void TerminalItemTests::cancelsTrackedSelectionWhenMouseGrabIsLost()
{
    TestableTerminalItem item;
    item.setSnapshot(snapshotAt(0, 0));
    item.setSize(QSizeF{800, 480});
    std::vector<ztermy::terminal::TerminalSelectionGesture> gestures;
    connect(&item, &ztermy::ui::TerminalItem::selectionGestureRequested, &item, [&gestures](const auto &gesture) {
        gestures.push_back(gesture);
    });

    const QRectF origin = item.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const QPointF start{origin.x() + (2.5 * origin.width()), origin.y() + (3.5 * origin.height())};
    const QPointF aboveViewport{start.x(), -origin.height()};
    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, aboveViewport, aboveViewport, aboveViewport, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    item.mouseMoveEvent(&move);
    item.mouseUngrabEvent();

    QVERIFY(!gestures.empty());
    QCOMPARE(gestures.back().type, ztermy::terminal::TerminalSelectionGestureType::cancel);
    const std::size_t cancelledCount = gestures.size();
    QTest::qWait(120);
    QCOMPARE(gestures.size(), cancelledCount);
}

void TerminalItemTests::reflectsSelectionStateFromSnapshots()
{
    TestableTerminalItem item;
    auto selected = snapshotAt(0, 0);
    selected->selectionPresent = true;
    item.setSnapshot(selected);
    QVERIFY(item.hasSelection());
    QVERIFY(item.selectionActionVisible());

    auto cleared = snapshotAt(0, 0);
    item.setSnapshot(cleared);
    QVERIFY(!item.hasSelection());
    QVERIFY(!item.selectionActionVisible());
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

    const int rowPixels = static_cast<int>(std::ceil(item.inputMethodQuery(Qt::ImCursorRectangle).toRectF().height()));
    QWheelEvent pixelEvent(QPointF{}, QPointF{}, QPoint{0, rowPixels}, QPoint{}, Qt::NoButton, Qt::NoModifier,
                           Qt::ScrollUpdate, false);
    item.wheelEvent(&pixelEvent);
    QVERIFY(pixelEvent.isAccepted());
    QCOMPARE(scrollSpy.count(), 3);
    QCOMPARE(scrollSpy.at(2).at(0).toInt(), -1);
}

void TerminalItemTests::routesTrackedMouseAndWheelToTerminal()
{
    TestableTerminalItem item;
    item.setSize(QSizeF{800, 480});
    auto snapshot = snapshotAt(0, 0);
    snapshot->mouseTrackingActive = true;
    item.setSnapshot(snapshot);

    std::vector<ztermy::terminal::TerminalMouseEvent> mouseEvents;
    QObject::connect(&item, &ztermy::ui::TerminalItem::mouseEventGenerated, &item,
                     [&mouseEvents](const ztermy::terminal::TerminalMouseEvent &event) {
                         mouseEvents.push_back(event);
                     });
    QSignalSpy selectionSpy(&item, &ztermy::ui::TerminalItem::selectionGestureRequested);
    QSignalSpy scrollSpy(&item, &ztermy::ui::TerminalItem::scrollRequested);

    const QPointF point{40, 40};
    QMouseEvent press(QEvent::MouseButtonPress, point, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QCOMPARE(mouseEvents.size(), std::size_t{1});
    QCOMPARE(mouseEvents.back().action, ztermy::terminal::TerminalMouseAction::press);
    QCOMPARE(mouseEvents.back().button, ztermy::terminal::TerminalMouseButton::left);
    QCOMPARE(selectionSpy.count(), 0);

    QWheelEvent wheel(point, point, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    item.wheelEvent(&wheel);
    QCOMPARE(mouseEvents.size(), std::size_t{2});
    QCOMPARE(mouseEvents.back().button, ztermy::terminal::TerminalMouseButton::four);
    QCOMPARE(scrollSpy.count(), 0);

    QMouseEvent shiftedPress(QEvent::MouseButtonPress, point, point, point, Qt::LeftButton, Qt::LeftButton,
                             Qt::ShiftModifier);
    item.mousePressEvent(&shiftedPress);
    QCOMPARE(mouseEvents.size(), std::size_t{2});
    QCOMPARE(selectionSpy.count(), 1);
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

    // Keyboard bindings are dispatched exclusively by ActionRegistry/Main.qml
    // so customized or cleared shortcuts cannot fall through to hard-coded
    // defaults in TerminalItem.
    item.scrollPage(-1);
    item.scrollPage(1);
    item.scrollLines(-1);
    item.scrollToFraction(0.0);
    item.scrollToFraction(1.0);

    QCOMPARE(scrollSpy.count(), 6);
    QCOMPARE(scrollSpy.at(1).at(0).toInt(), -23);
    QCOMPARE(scrollSpy.at(2).at(0).toInt(), 23);
    QCOMPARE(scrollSpy.at(3).at(0).toInt(), -1);
    QCOMPARE(scrollSpy.at(4).at(0).toInt(), -40);
    QCOMPARE(scrollSpy.at(5).at(0).toInt(), 40);
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

void TerminalItemTests::keepsBaseTextureDuringCursorBlink()
{
    QQuickWindow window;
    auto *item = new TestableTerminalItem(window.contentItem());
    item->setPerformanceMetricsEnabled(true);
    item->setSnapshot(snapshotAt(2, 2));
    window.resize(640, 220);
    item->setSize(window.size());
    window.show();
    QTest::qWait(100);
    QVERIFY(!window.grabWindow().isNull());

    item->resetPerformanceMetrics();
    QTRY_VERIFY_WITH_TIMEOUT(item->performanceMetrics().cursorInvalidations >= 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(item->performanceMetrics().renderedFrames >= 1, 1000);
    const ztermy::ui::TerminalRenderMetricsSnapshot metrics = item->performanceMetrics();
    QCOMPARE(metrics.uploadedBytes, std::uint64_t{0});
    QVERIFY(metrics.partialFrames >= 1);

    window.close();
    QCoreApplication::processEvents();
}

} // namespace

QTEST_MAIN(TerminalItemTests)

#include "terminal_item_tests.moc"
