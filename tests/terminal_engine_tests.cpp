#include "domain/terminal/GhosttyTerminalEngine.h"

#include <QTest>

#include <span>
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
};

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

    constexpr std::string_view content = "\x1b[38;2;12;34;56mA\x1b[0m";
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
    QVERIFY(snapshot->cursor.visible);
    QCOMPARE(snapshot->cursor.column, 1);
    QCOMPARE(snapshot->cursor.row, 0);
}

} // namespace

QTEST_GUILESS_MAIN(TerminalEngineTests)

#include "terminal_engine_tests.moc"
