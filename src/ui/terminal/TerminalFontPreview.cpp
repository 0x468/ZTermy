#include "ui/terminal/TerminalFontPreview.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace ztermy::ui
{
TerminalFontPreview::TerminalFontPreview(QQuickItem *parent) : TerminalItem(parent)
{
    setReadOnly(true);
    setActiveFocusOnTab(false);
    setAcceptedMouseButtons(Qt::NoButton);
    setTerminalCursorVisible(false);
    setTextBlinkEnabled(false);
    auto sample = std::make_shared<terminal::TerminalSnapshot>();
    sample->columns = 64;
    sample->rows = 4;
    sample->defaultForeground = {.red = 220, .green = 220, .blue = 220};
    sample->defaultBackground = {.red = 20, .green = 20, .blue = 20};
    sample->cells.resize(static_cast<std::size_t>(sample->columns) * sample->rows);
    const std::array<std::u32string_view, 4> lines{
        U"PS C:\\Projects> git status --short",
        U"AaBb 0123456789  {} [] ()  -> => != ===",
        U"问君能有几多愁  Underneath the Stars",
        U"Regular  Bold  Italic  https://example.com",
    };
    for (std::uint16_t row = 0; row < sample->rows; ++row)
    {
        std::uint16_t column = 0;
        for (const char32_t character : lines[row])
        {
            const std::uint8_t width = character >= 0x2e80 && character <= 0x9fff ? 2 : 1;
            if (column + width > sample->columns)
                break;
            auto &cell = sample->cell(column, row);
            cell.grapheme = character;
            cell.foreground = sample->defaultForeground;
            cell.displayWidth = width;
            cell.bold = row == 3 && column >= 9 && column < 13;
            cell.italic = (row == 2 && column >= 16) || (row == 3 && column >= 15 && column < 21);
            if (width == 2)
                sample->cell(column + 1, row).displayWidth = 0;
            column += width;
        }
    }
    setSnapshot(std::move(sample));
}

} // namespace ztermy::ui
