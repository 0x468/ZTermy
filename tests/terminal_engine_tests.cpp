#include "domain/terminal/GhosttyTerminalEngine.h"

#include <QTest>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{

class TerminalEngineTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidGeometry();
    void parsesSplitVtSequences();
    void preservesContentAcrossResize();
    void exposesImmutableStyledCells();
    void exposesWideCellAndCursorWidth();
    void preservesPrimaryScreenAcrossAlternateScreen();
    void normalizesWideCellSelection();
    void handlesEraseAndCursorVisibility();
    void exposesCombiningAndEmojiGraphemes();
    void reportsAndResetsRenderDamage();
    void selectsAndFormatsViewportText();
    void appliesTrackedWordSelectionGestures();
    void autoscrollsTrackedSelectionWithoutDoubleScrolling();
    void selectsCompleteScrollback();
    void scrollsThroughHistory();
    void searchesAcrossScrollbackAndWrappedLines();
    void encodesPasteForTerminalMode();
    void exposesShellWorkingDirectorySequences();
    void pagesThroughScrollback();
};

void TerminalEngineTests::exposesShellWorkingDirectorySequences()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 4});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view osc7 = "\x1b]7;file://host/home/test/My%20Files\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(osc7))));
    auto snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->workingDirectory, std::string("file://host/home/test/My%20Files"));

    constexpr std::string_view osc1337 = "\x1b]1337;CurrentDir=/srv/project\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(osc1337))));
    snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->workingDirectory, std::string("/srv/project"));
}

void TerminalEngineTests::rejectsInvalidGeometry()
{
    const auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 0, .rows = 24});

    QVERIFY(!result);
    QCOMPARE(result.error(), std::make_error_code(std::errc::invalid_argument));
}

void TerminalEngineTests::parsesSplitVtSequences()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 80, .rows = 24});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view first = "\x1b[3";
    constexpr std::string_view second = "1mred\x1b[0m\r\nplain";
    QVERIFY(!engine.feed(std::as_bytes(std::span(first))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(second))));

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "red\nplain");
}

void TerminalEngineTests::preservesContentAcrossResize()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "abcdefghij";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.resize({.columns = 5, .rows = 3, .cellWidthPixels = 9, .cellHeightPixels = 18}));

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "abcdefghij");
}

void TerminalEngineTests::exposesImmutableStyledCells()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "\x1b[38;2;12;34;56mA\x1b[48;2;7;8;9mB\x1b[0m";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }

    QCOMPARE(snapshot->columns, 12);
    QCOMPARE(snapshot->rows, 2);
    QCOMPARE(snapshot->cells.size(), std::size_t{24});
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"A"));
    QCOMPARE(snapshot->cell(0, 0).foreground, (ztermy::terminal::TerminalColor{12, 34, 56}));
    QVERIFY(!snapshot->cell(0, 0).explicitBackground);
    QCOMPARE(snapshot->cell(1, 0).background, (ztermy::terminal::TerminalColor{7, 8, 9}));
    QVERIFY(snapshot->cell(1, 0).explicitBackground);
    QVERIFY(!snapshot->cell(2, 0).explicitBackground);
    QVERIFY(!snapshot->cell(11, 1).explicitBackground);
    QVERIFY(snapshot->cursor.visible);
    QCOMPARE(snapshot->cursor.column, 2);
    QCOMPARE(snapshot->cursor.row, 0);
}

void TerminalEngineTests::exposesWideCellAndCursorWidth()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"中\x1b[D";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"中"));
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cursor.column, 0);
    QCOMPARE(snapshot->cursor.width, std::uint8_t{2});
}

void TerminalEngineTests::preservesPrimaryScreenAcrossAlternateScreen()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view primary = "primary";
    constexpr std::string_view enterAlternate = "\x1b[?1049h\x1b[H";
    constexpr std::string_view alternate = "alternate";
    constexpr std::string_view leaveAlternate = "\x1b[?1049l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(primary))));
    const auto primarySnapshot = engine.snapshot();
    QVERIFY(primarySnapshot);
    QCOMPARE(primarySnapshot->cursor.column, std::uint16_t{7});
    QCOMPARE(primarySnapshot->cursor.row, std::uint16_t{0});
    QVERIFY(!engine.feed(std::as_bytes(std::span(enterAlternate))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(alternate))));

    auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "alternate");

    QVERIFY(!engine.feed(std::as_bytes(std::span(leaveAlternate))));
    text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "primary");
    const auto restoredSnapshot = engine.snapshot();
    QVERIFY(restoredSnapshot);
    QCOMPARE(restoredSnapshot->cursor.column, primarySnapshot->cursor.column);
    QCOMPARE(restoredSnapshot->cursor.row, primarySnapshot->cursor.row);
}

void TerminalEngineTests::normalizesWideCellSelection()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"中";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.setSelection(
        ztermy::terminal::TerminalSelection{.start = {.column = 1, .row = 0}, .end = {.column = 1, .row = 0}}));

    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{0});
    QVERIFY(snapshot->cell(0, 0).selected);
    QVERIFY(snapshot->cell(1, 0).selected);
}

void TerminalEngineTests::handlesEraseAndCursorVisibility()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "first\r\nsecond";
    constexpr std::string_view hideCursor = "\x1b[?25l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(hideCursor))));

    auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QVERIFY(!snapshot->cursor.visible);

    constexpr std::string_view clearAndShowCursor = "\x1b[2J\x1b[H\x1b[?25h\x1b[6 q";
    QVERIFY(!engine.feed(std::as_bytes(std::span(clearAndShowCursor))));
    snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QVERIFY(snapshot->cursor.visible);
    QCOMPARE(snapshot->cursor.column, std::uint16_t{0});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{0});
    QCOMPARE(snapshot->cursor.style, ztermy::terminal::TerminalCursorStyle::bar);
    QVERIFY(!snapshot->cursor.blinking);

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "");
}

void TerminalEngineTests::exposesCombiningAndEmojiGraphemes()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"e\u0301中🙂";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"e\u0301"));
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{1});
    QCOMPARE(snapshot->cell(1, 0).grapheme, std::u32string(U"中"));
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(2, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cell(3, 0).grapheme, std::u32string(U"🙂"));
    QCOMPARE(snapshot->cell(3, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(4, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cursor.column, std::uint16_t{5});
}

void TerminalEngineTests::reportsAndResetsRenderDamage()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const auto initial = engine.snapshot();
    QVERIFY(initial);
    QCOMPARE(initial->damage, ztermy::terminal::TerminalDamageKind::full);

    const auto clean = engine.snapshot();
    QVERIFY(clean);
    QCOMPARE(clean->damage, ztermy::terminal::TerminalDamageKind::none);
    QVERIFY(clean->damagedRows.empty());

    constexpr std::string_view content = "changed";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    const auto changed = engine.snapshot();
    QVERIFY(changed);
    QVERIFY(changed->damage != ztermy::terminal::TerminalDamageKind::none);
    if (changed->damage == ztermy::terminal::TerminalDamageKind::partial)
    {
        QVERIFY(std::ranges::find(changed->damagedRows, std::uint16_t{0}) != changed->damagedRows.end());
    }

    QVERIFY(!engine.resize({.columns = 16, .rows = 4}));
    const auto resized = engine.snapshot();
    QVERIFY(resized);
    QCOMPARE(resized->damage, ztermy::terminal::TerminalDamageKind::full);
}

void TerminalEngineTests::selectsAndFormatsViewportText()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "alpha beta\r\nsecond";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.setSelection(
        ztermy::terminal::TerminalSelection{.start = {.column = 0, .row = 0}, .end = {.column = 4, .row = 0}}));

    const auto selectedText = engine.selectedText();
    if (!selectedText)
    {
        QFAIL(selectedText.error().message().c_str());
    }
    QVERIFY(selectedText->has_value());
    QCOMPARE(**selectedText, "alpha");

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    for (std::uint16_t column = 0; column < 5; ++column)
    {
        QVERIFY(snapshot->cell(column, 0).selected);
    }
    QVERIFY(snapshot->selectionPresent);
    QVERIFY(!snapshot->cell(5, 0).selected);

    QVERIFY(!engine.setSelection(std::nullopt));
    const auto clearedText = engine.selectedText();
    QVERIFY(clearedText);
    QVERIFY(!clearedText->has_value());
}

void TerminalEngineTests::autoscrollsTrackedSelectionWithoutDoubleScrolling()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;
    constexpr std::string_view content =
        "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\nline8";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    ztermy::terminal::TerminalSelectionGesture press{
        .type = ztermy::terminal::TerminalSelectionGestureType::press,
        .point = {.column = 2, .row = 1},
        .positionX = 20.0,
        .positionY = 24.0,
        .eventTimeNanoseconds = 1'000'000'000,
        .repeatIntervalNanoseconds = 500'000'000,
        .repeatDistancePixels = 4.0,
    };
    auto changed = engine.applySelectionGesture(press);
    QVERIFY(changed);

    auto drag = press;
    drag.type = ztermy::terminal::TerminalSelectionGestureType::drag;
    drag.point = {.column = 2, .row = 0};
    drag.positionY = 0.0;
    drag.columns = 12;
    drag.cellWidthPixels = 8;
    drag.screenHeightPixels = 48;
    changed = engine.applySelectionGesture(drag);
    QVERIFY(changed);
    QVERIFY(*changed);

    const auto before = engine.snapshot();
    QVERIFY(before);
    QVERIFY(before->scrollbar.offset >= 3);

    auto tick = drag;
    tick.type = ztermy::terminal::TerminalSelectionGestureType::autoscrollTick;
    tick.scrollRows = -3;
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    QVERIFY(*changed);

    const auto after = engine.snapshot();
    QVERIFY(after);
    QCOMPARE(before->scrollbar.offset - after->scrollbar.offset, std::uint64_t{3});
    QVERIFY(after->selectionPresent);

    tick.scrollRows = -64;
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    const auto atTop = engine.snapshot();
    QVERIFY(atTop);
    QCOMPARE(atTop->scrollbar.offset, std::uint64_t{0});
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    QVERIFY(!*changed);
}

void TerminalEngineTests::appliesTrackedWordSelectionGestures()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 40, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;
    constexpr std::string_view content = "user@host:/var/log/app.log ready";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto press = ztermy::terminal::TerminalSelectionGesture{
        .type = ztermy::terminal::TerminalSelectionGestureType::press,
        .point = {.column = 4, .row = 0},
        .positionX = 36.0,
        .positionY = 8.0,
        .eventTimeNanoseconds = 1'000'000'000,
        .repeatIntervalNanoseconds = 500'000'000,
        .repeatDistancePixels = 4.0,
        .wordBoundaryCodepoints = U" \t'\"│`|;,()[]{}<>$",
    };
    const auto firstPress = engine.applySelectionGesture(press);
    QVERIFY(firstPress);
    QVERIFY(*firstPress);
    auto release = press;
    release.type = ztermy::terminal::TerminalSelectionGestureType::release;
    const auto firstRelease = engine.applySelectionGesture(release);
    QVERIFY(firstRelease);
    QVERIFY(!*firstRelease);
    press.eventTimeNanoseconds += 100'000'000;
    const auto secondPress = engine.applySelectionGesture(press);
    QVERIFY(secondPress);
    QVERIFY(*secondPress);

    const auto selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "user@host:/var/log/app.log");
}

void TerminalEngineTests::selectsCompleteScrollback()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;
    constexpr std::string_view content = "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.selectAll());

    const auto selected = engine.selectedText();
    QVERIFY(selected);
    if (!selected->has_value())
    {
        QFAIL("selectAll did not produce selected text");
    }
    const auto &selectedText = **selected;
    QVERIFY(selectedText.starts_with("line0"));
    QVERIFY(selectedText.ends_with("line5"));
}

void TerminalEngineTests::scrollsThroughHistory()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto bottom = engine.snapshot();
    if (!bottom)
    {
        QFAIL(bottom.error().message().c_str());
    }
    QVERIFY(bottom->scrollbar.total > bottom->scrollbar.visible);
    const std::uint64_t bottomOffset = bottom->scrollbar.offset;

    engine.scrollViewport(-2);
    const auto history = engine.snapshot();
    if (!history)
    {
        QFAIL(history.error().message().c_str());
    }
    QVERIFY(history->scrollbar.offset < bottomOffset);
    QVERIFY(!history->cursor.visible);

    engine.scrollToBottom();
    const auto restored = engine.snapshot();
    QVERIFY(restored);
    QCOMPARE(restored->scrollbar.offset, bottomOffset);
}

void TerminalEngineTests::searchesAcrossScrollbackAndWrappedLines()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"Alpha one\r\nprefix alpha suffix\r\n中间\r\nlast ALPHA";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::forward, false);
    if (!search)
    {
        QFAIL(search.error().message().c_str());
    }
    QCOMPARE(search->current, std::uint32_t{1});
    QCOMPARE(search->total, std::uint32_t{3});
    QVERIFY(!search->wrapped);

    auto selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "Alpha");

    search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::forward, false);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{2});
    QCOMPARE(search->total, std::uint32_t{3});

    search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::backward, false);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{1});

    const std::u8string_view unicodeQuery = u8"中间";
    search = engine.search(std::string_view(reinterpret_cast<const char *>(unicodeQuery.data()), unicodeQuery.size()),
                           ztermy::terminal::TerminalSearchDirection::forward, true);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{1});
    QCOMPARE(search->total, std::uint32_t{1});

    QVERIFY(!engine.clearSearch());
    selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(!selected->has_value());
}

void TerminalEngineTests::encodesPasteForTerminalMode()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view text = "a\nb";
    auto encoded = engine.encodePaste(std::as_bytes(std::span(text)));
    if (!encoded)
    {
        QFAIL(encoded.error().message().c_str());
    }
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "a\rb");

    constexpr std::string_view enableBracketedPaste = "\x1b[?2004h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(enableBracketedPaste))));
    encoded = engine.encodePaste(std::as_bytes(std::span(text)));
    if (!encoded)
    {
        QFAIL(encoded.error().message().c_str());
    }
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "\x1b[200~a\nb\x1b[201~");
}

void TerminalEngineTests::pagesThroughScrollback()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "line-00\r\nline-11\r\nline-22\r\nline-33\r\nline-44\r\nline-55";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto page = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 0, .lineCount = 10});
    if (!page)
    {
        QFAIL(page.error().message().c_str());
    }
    QCOMPARE(page->totalLines, std::size_t{6});
    QCOMPARE(page->scrollbackLines, std::size_t{3});
    QCOMPARE(page->lines.size(), std::size_t{6});
    QCOMPARE(page->lines.at(0), std::string("line-00"));
    QCOMPARE(page->lines.at(5), std::string("line-55"));

    // A bounded page from the middle.
    auto middle = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 2, .lineCount = 2});
    if (!middle)
    {
        QFAIL(middle.error().message().c_str());
    }
    QCOMPARE(middle->firstLine, std::size_t{2});
    QCOMPARE(middle->lines.size(), std::size_t{2});
    QCOMPARE(middle->lines.at(0), std::string("line-22"));
    QCOMPARE(middle->lines.at(1), std::string("line-33"));

    // Out-of-range requests return an empty page instead of failing.
    auto beyond = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 100, .lineCount = 4});
    if (!beyond)
    {
        QFAIL(beyond.error().message().c_str());
    }
    QVERIFY(beyond->lines.empty());
    QCOMPARE(beyond->firstLine, std::size_t{100});

    // Invalid arguments are rejected.
    QVERIFY(!engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 0, .lineCount = 0}));

    auto tail = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::tail, .offset = 0, .lineCount = 2});
    if (!tail)
    {
        QFAIL(tail.error().message().c_str());
    }
    QCOMPARE(tail->firstLine, std::size_t{4});
    QCOMPARE(tail->lines, std::vector<std::string>({"line-44", "line-55"}));

    auto olderTail = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::tail, .offset = 2, .lineCount = 2});
    if (!olderTail)
    {
        QFAIL(olderTail.error().message().c_str());
    }
    QCOMPARE(olderTail->firstLine, std::size_t{2});
    QCOMPARE(olderTail->lines, std::vector<std::string>({"line-22", "line-33"}));
}

} // namespace

QTEST_GUILESS_MAIN(TerminalEngineTests)

#include "terminal_engine_tests.moc"
