#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
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

enum class TerminalCursorStyle : std::uint8_t
{
    block,
    bar,
    underline,
    hollowBlock,
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
    std::vector<TerminalCell> cells;

    [[nodiscard]] const TerminalCell &cell(const std::uint16_t column, const std::uint16_t row) const
    {
        return cells.at((static_cast<std::size_t>(row) * columns) + column);
    }
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
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code>
    encodePaste(std::span<const std::byte> bytes) const = 0;

    // Diagnostic representation of the active screen.
    [[nodiscard]] virtual std::expected<std::string, std::error_code> plainText() const = 0;

protected:
    TerminalEngine() = default;
};

} // namespace ztermy::terminal
