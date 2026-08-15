#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ztermy::terminal
{

struct TerminalGeometry
{
    std::uint16_t columns = 80;
    std::uint16_t rows = 24;
    std::uint32_t cellWidthPixels = 0;
    std::uint32_t cellHeightPixels = 0;

    [[nodiscard]] bool valid() const noexcept { return columns > 0 && rows > 0; }
};

struct TerminalColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const TerminalColor &, const TerminalColor &) = default;
};

struct TerminalCell
{
    std::u32string grapheme;
    TerminalColor foreground;
    TerminalColor background;
    bool explicitBackground = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool overline = false;
    bool invisible = false;
    bool selected = false;
    std::uint8_t displayWidth = 1;
};

struct TerminalPoint
{
    std::uint16_t column = 0;
    std::uint16_t row = 0;

    friend bool operator==(const TerminalPoint &, const TerminalPoint &) = default;
};

struct TerminalSelection
{
    TerminalPoint start;
    TerminalPoint end;
    bool rectangular = false;
};

struct TerminalScrollbar
{
    std::uint64_t total = 0;
    std::uint64_t offset = 0;
    std::uint64_t visible = 0;
};

enum class TerminalSearchDirection : std::uint8_t
{
    forward,
    backward,
};

struct TerminalSearchResult
{
    std::uint32_t current = 0;
    std::uint32_t total = 0;
    bool wrapped = false;
};

enum class TerminalCursorStyle : std::uint8_t
{
    block,
    bar,
    underline,
    hollowBlock,
};

enum class TerminalDamageKind : std::uint8_t
{
    none,
    partial,
    full,
};

struct TerminalCursor
{
    std::uint16_t column = 0;
    std::uint16_t row = 0;
    std::uint8_t width = 1;
    TerminalCursorStyle style = TerminalCursorStyle::block;
    TerminalColor color;
    bool visible = false;
    bool blinking = false;
};

struct TerminalSnapshot
{
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    TerminalColor defaultForeground;
    TerminalColor defaultBackground;
    TerminalCursor cursor;
    TerminalScrollbar scrollbar;
    TerminalDamageKind damage = TerminalDamageKind::full;
    std::vector<std::uint16_t> damagedRows;
    std::vector<TerminalCell> cells;
    // Raw value emitted by OSC 7 / OSC 9 / OSC 1337. Consumers must
    // validate and normalize it before treating it as a filesystem path.
    std::string workingDirectory;

    [[nodiscard]] const TerminalCell &cell(const std::uint16_t column, const std::uint16_t row) const
    {
        return cells.at((static_cast<std::size_t>(row) * columns) + column);
    }

    [[nodiscard]] TerminalCell &cell(const std::uint16_t column, const std::uint16_t row)
    {
        return cells.at((static_cast<std::size_t>(row) * columns) + column);
    }
};

// A bounded page of scrollback text. Line 0 is the top of the retained
// history (the oldest scrollback row); the active screen rows follow, so the
// page space is "full screen including scrollback" (the same space as
// GhosttyPoint SCREEN coordinates).
struct TerminalScrollbackPage final
{
    std::vector<std::string> lines;
    std::size_t firstLine = 0;
    std::size_t totalLines = 0;
    std::size_t scrollbackLines = 0;

    friend bool operator==(const TerminalScrollbackPage &, const TerminalScrollbackPage &) = default;
};

enum class TerminalScrollbackAnchor : std::uint8_t
{
    head,
    tail,
};

// `offset` is measured from the selected anchor. For head reads it is the
// zero-based absolute line index. For tail reads it is the number of newest
// lines to skip, which makes paging recent output deterministic while the
// terminal keeps growing.
struct TerminalScrollbackRequest final
{
    TerminalScrollbackAnchor anchor = TerminalScrollbackAnchor::head;
    std::size_t offset = 0;
    std::size_t lineCount = 1;
};

using TerminalSnapshotPtr = std::shared_ptr<const TerminalSnapshot>;

class TerminalEngine
{
public:
    virtual ~TerminalEngine() = default;

    TerminalEngine(const TerminalEngine &) = delete;
    TerminalEngine &operator=(const TerminalEngine &) = delete;
    TerminalEngine(TerminalEngine &&) = delete;
    TerminalEngine &operator=(TerminalEngine &&) = delete;

    [[nodiscard]] virtual std::error_code feed(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::error_code resize(TerminalGeometry geometry) = 0;
    [[nodiscard]] virtual std::expected<TerminalSnapshot, std::error_code> snapshot() = 0;
    [[nodiscard]] virtual std::error_code setSelection(std::optional<TerminalSelection> selection) = 0;
    [[nodiscard]] virtual std::expected<std::optional<std::string>, std::error_code> selectedText() const = 0;
    virtual void scrollViewport(int rows) = 0;
    virtual void scrollToBottom() = 0;
    [[nodiscard]] virtual std::expected<TerminalSearchResult, std::error_code>
    search(std::string_view query, TerminalSearchDirection direction, bool caseSensitive) = 0;
    [[nodiscard]] virtual std::error_code clearSearch() = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code>
    encodePaste(std::span<const std::byte> bytes) const = 0;

    // Read a bounded page of scrollback text (including the active screen at
    // the tail). firstLine is zero-based from the top of the retained
    // history. Head offsets address absolute retained lines; tail offsets skip
    // newest lines before selecting a page. This API is not intended for the
    // render path; it is used by AI/context tools to page through output that
    // scrolled off the screen.
    [[nodiscard]] virtual std::expected<TerminalScrollbackPage, std::error_code>
    scrollbackPage(TerminalScrollbackRequest request) const = 0;

    // Diagnostic representation of the active screen.
    [[nodiscard]] virtual std::expected<std::string, std::error_code> plainText() const = 0;

protected:
    TerminalEngine() = default;
};

} // namespace ztermy::terminal
