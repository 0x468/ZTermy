#include "domain/terminal/GhosttyTerminalEngine.h"

#include <ghostty/vt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace
{

class GhosttyErrorCategory final : public std::error_category
{
public:
    [[nodiscard]] const char *name() const noexcept override { return "libghostty-vt"; }

    [[nodiscard]] std::string message(const int condition) const override
    {
        switch (static_cast<GhosttyResult>(condition))
        {
            case GHOSTTY_SUCCESS:
                return "success";
            case GHOSTTY_OUT_OF_MEMORY:
                return "out of memory";
            case GHOSTTY_INVALID_VALUE:
                return "invalid value";
            case GHOSTTY_OUT_OF_SPACE:
                return "output buffer is too small";
            case GHOSTTY_NO_VALUE:
                return "requested value is unavailable";
            default:
                return "unknown libghostty-vt error";
        }
    }
};

[[nodiscard]] const std::error_category &ghosttyErrorCategory() noexcept
{
    static GhosttyErrorCategory category;
    return category;
}

[[nodiscard]] std::error_code ghosttyError(const GhosttyResult result) noexcept
{
    return {static_cast<int>(result), ghosttyErrorCategory()};
}

[[nodiscard]] std::error_code invalidArgument() noexcept
{
    return std::make_error_code(std::errc::invalid_argument);
}

class UniqueTerminal final
{
public:
    explicit UniqueTerminal(const GhosttyTerminal handle) noexcept : m_handle(handle) {}

    ~UniqueTerminal()
    {
        if (m_handle != nullptr)
        {
            ghostty_terminal_free(m_handle);
        }
    }

    UniqueTerminal(const UniqueTerminal &) = delete;
    UniqueTerminal &operator=(const UniqueTerminal &) = delete;

    [[nodiscard]] GhosttyTerminal get() const noexcept { return m_handle; }
    void release() noexcept { m_handle = nullptr; }

private:
    GhosttyTerminal m_handle;
};

class UniqueRenderState final
{
public:
    explicit UniqueRenderState(const GhosttyRenderState handle) noexcept : m_handle(handle) {}
    ~UniqueRenderState()
    {
        if (m_handle != nullptr)
        {
            ghostty_render_state_free(m_handle);
        }
    }

    UniqueRenderState(const UniqueRenderState &) = delete;
    UniqueRenderState &operator=(const UniqueRenderState &) = delete;

    [[nodiscard]] GhosttyRenderState get() const noexcept { return m_handle; }
    void release() noexcept { m_handle = nullptr; }

private:
    GhosttyRenderState m_handle;
};

class UniqueRowIterator final
{
public:
    explicit UniqueRowIterator(const GhosttyRenderStateRowIterator handle) noexcept : m_handle(handle) {}
    ~UniqueRowIterator()
    {
        if (m_handle != nullptr)
        {
            ghostty_render_state_row_iterator_free(m_handle);
        }
    }

    UniqueRowIterator(const UniqueRowIterator &) = delete;
    UniqueRowIterator &operator=(const UniqueRowIterator &) = delete;

    [[nodiscard]] GhosttyRenderStateRowIterator get() const noexcept { return m_handle; }
    void release() noexcept { m_handle = nullptr; }

private:
    GhosttyRenderStateRowIterator m_handle;
};

class UniqueRowCells final
{
public:
    explicit UniqueRowCells(const GhosttyRenderStateRowCells handle) noexcept : m_handle(handle) {}
    ~UniqueRowCells()
    {
        if (m_handle != nullptr)
        {
            ghostty_render_state_row_cells_free(m_handle);
        }
    }

    UniqueRowCells(const UniqueRowCells &) = delete;
    UniqueRowCells &operator=(const UniqueRowCells &) = delete;

    [[nodiscard]] GhosttyRenderStateRowCells get() const noexcept { return m_handle; }
    void release() noexcept { m_handle = nullptr; }

private:
    GhosttyRenderStateRowCells m_handle;
};

class UniqueFormatter final
{
public:
    ~UniqueFormatter()
    {
        if (handle != nullptr)
        {
            ghostty_formatter_free(handle);
        }
    }

    UniqueFormatter(const UniqueFormatter &) = delete;
    UniqueFormatter &operator=(const UniqueFormatter &) = delete;

    GhosttyFormatter handle = nullptr;

private:
    UniqueFormatter() = default;

    friend class ztermy::terminal::GhosttyTerminalEngine;
};

class GhosttyOwnedBuffer final
{
public:
    ~GhosttyOwnedBuffer()
    {
        if (data != nullptr)
        {
            ghostty_free(nullptr, data, length);
        }
    }

    GhosttyOwnedBuffer(const GhosttyOwnedBuffer &) = delete;
    GhosttyOwnedBuffer &operator=(const GhosttyOwnedBuffer &) = delete;

    std::uint8_t *data = nullptr;
    std::size_t length = 0;

private:
    GhosttyOwnedBuffer() = default;

    friend class ztermy::terminal::GhosttyTerminalEngine;
};

[[nodiscard]] ztermy::terminal::TerminalColor terminalColor(const GhosttyColorRgb color) noexcept
{
    return {.red = color.r, .green = color.g, .blue = color.b};
}

void appendUtf8(std::string &destination, const std::uint32_t codepoint)
{
    if (codepoint <= 0x7FU)
    {
        destination.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FFU)
    {
        destination.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        destination.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    else if (codepoint <= 0xFFFFU)
    {
        destination.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        destination.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        destination.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    else
    {
        destination.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        destination.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        destination.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        destination.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

[[nodiscard]] std::string asciiFold(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char byte) {
        return byte < 0x80U ? static_cast<char>(std::tolower(byte)) : static_cast<char>(byte);
    });
    return value;
}

} // namespace

namespace ztermy::terminal
{

struct GhosttyTerminalEngine::Impl
{
    Impl(const GhosttyTerminal terminalHandle, const GhosttyRenderState renderStateHandle,
         const GhosttyRenderStateRowIterator rowIteratorHandle,
         const GhosttyRenderStateRowCells rowCellsHandle) noexcept
        : terminal(terminalHandle),
          renderState(renderStateHandle),
          rowIterator(rowIteratorHandle),
          rowCells(rowCellsHandle)
    {
    }

    ~Impl()
    {
        if (rowCells != nullptr)
        {
            ghostty_render_state_row_cells_free(rowCells);
        }
        if (rowIterator != nullptr)
        {
            ghostty_render_state_row_iterator_free(rowIterator);
        }
        if (renderState != nullptr)
        {
            ghostty_render_state_free(renderState);
        }
        if (terminal != nullptr)
        {
            ghostty_terminal_free(terminal);
        }
    }

    GhosttyTerminal terminal = nullptr;
    GhosttyRenderState renderState = nullptr;
    GhosttyRenderStateRowIterator rowIterator = nullptr;
    GhosttyRenderStateRowCells rowCells = nullptr;
    std::string lastSearchQuery;
    bool lastSearchCaseSensitive = false;
};

std::expected<std::unique_ptr<GhosttyTerminalEngine>, std::error_code>
GhosttyTerminalEngine::create(const TerminalGeometry geometry)
{
    if (!geometry.valid())
    {
        return std::unexpected(invalidArgument());
    }

    GhosttyTerminal terminal = nullptr;
    const GhosttyResult result = ghostty_terminal_new(nullptr, &terminal, geometry.columns, geometry.rows);
    if (result != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(result));
    }

    UniqueTerminal terminalOwner(terminal);

    GhosttyRenderState renderState = nullptr;
    if (const GhosttyResult renderResult = ghostty_render_state_new(nullptr, &renderState);
        renderResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(renderResult));
    }
    UniqueRenderState renderStateOwner(renderState);

    GhosttyRenderStateRowIterator rowIterator = nullptr;
    if (const GhosttyResult iteratorResult = ghostty_render_state_row_iterator_new(nullptr, &rowIterator);
        iteratorResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(iteratorResult));
    }
    UniqueRowIterator rowIteratorOwner(rowIterator);

    GhosttyRenderStateRowCells rowCells = nullptr;
    if (const GhosttyResult cellsResult = ghostty_render_state_row_cells_new(nullptr, &rowCells);
        cellsResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(cellsResult));
    }
    UniqueRowCells rowCellsOwner(rowCells);

    auto impl = std::make_unique<Impl>(terminalOwner.get(), renderStateOwner.get(), rowIteratorOwner.get(),
                                       rowCellsOwner.get());
    terminalOwner.release();
    renderStateOwner.release();
    rowIteratorOwner.release();
    rowCellsOwner.release();
    auto engine = std::unique_ptr<GhosttyTerminalEngine>(new GhosttyTerminalEngine(std::move(impl)));
    if (const std::error_code resizeError = engine->resize(geometry))
    {
        return std::unexpected(resizeError);
    }
    return engine;
}

GhosttyTerminalEngine::GhosttyTerminalEngine(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

GhosttyTerminalEngine::~GhosttyTerminalEngine() = default;

std::error_code GhosttyTerminalEngine::feed(const std::span<const std::byte> bytes)
{
    if (!bytes.empty())
    {
        ghostty_terminal_vt_write(m_impl->terminal, reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    }
    return {};
}

std::error_code GhosttyTerminalEngine::resize(const TerminalGeometry geometry)
{
    if (!geometry.valid())
    {
        return invalidArgument();
    }

    const GhosttyResult result = ghostty_terminal_resize(m_impl->terminal, geometry.columns, geometry.rows,
                                                         geometry.cellWidthPixels, geometry.cellHeightPixels);
    return result == GHOSTTY_SUCCESS ? std::error_code{} : ghosttyError(result);
}

std::error_code GhosttyTerminalEngine::setSelection(const std::optional<TerminalSelection> selection)
{
    m_impl->lastSearchQuery.clear();
    if (!selection)
    {
        const GhosttyResult result = ghostty_terminal_set(m_impl->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
        return result == GHOSTTY_SUCCESS ? std::error_code{} : ghosttyError(result);
    }

    const auto gridRef = [this](const TerminalPoint point) -> std::expected<GhosttyGridRef, std::error_code> {
        GhosttyPoint ghosttyPoint{};
        ghosttyPoint.tag = GHOSTTY_POINT_TAG_VIEWPORT;
        ghosttyPoint.value.coordinate = {.x = point.column, .y = point.row};

        GhosttyGridRef result{};
        if (const GhosttyResult refResult = ghostty_terminal_grid_ref(m_impl->terminal, ghosttyPoint, &result);
            refResult != GHOSTTY_SUCCESS)
        {
            return std::unexpected(ghosttyError(refResult));
        }
        return result;
    };

    auto start = gridRef(selection->start);
    if (!start)
    {
        return start.error();
    }
    auto end = gridRef(selection->end);
    if (!end)
    {
        return end.error();
    }

    GhosttySelection ghosttySelection{};
    ghosttySelection.size = sizeof(ghosttySelection);
    ghosttySelection.start = *start;
    ghosttySelection.end = *end;
    ghosttySelection.rectangle = selection->rectangular;
    const GhosttyResult result =
        ghostty_terminal_set(m_impl->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &ghosttySelection);
    return result == GHOSTTY_SUCCESS ? std::error_code{} : ghosttyError(result);
}

std::expected<std::optional<std::string>, std::error_code> GhosttyTerminalEngine::selectedText() const
{
    GhosttyTerminalSelectionFormatOptions options{};
    options.size = sizeof(options);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = true;
    options.trim = true;

    std::size_t required = 0;
    const GhosttyResult sizeResult =
        ghostty_terminal_selection_format_buf(m_impl->terminal, options, nullptr, 0, &required);
    if (sizeResult == GHOSTTY_NO_VALUE)
    {
        return std::optional<std::string>{};
    }
    if (sizeResult != GHOSTTY_OUT_OF_SPACE && sizeResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(sizeResult));
    }
    if (required == 0)
    {
        return std::optional<std::string>{std::in_place};
    }

    std::string result(required, '\0');
    std::size_t written = 0;
    if (const GhosttyResult formatResult = ghostty_terminal_selection_format_buf(
            m_impl->terminal, options, reinterpret_cast<std::uint8_t *>(result.data()), result.size(), &written);
        formatResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(formatResult));
    }
    result.resize(written);
    return std::optional<std::string>{std::move(result)};
}

void GhosttyTerminalEngine::scrollViewport(const int rows)
{
    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    behavior.value.delta = rows;
    ghostty_terminal_scroll_viewport(m_impl->terminal, behavior);
}

void GhosttyTerminalEngine::scrollToBottom()
{
    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    ghostty_terminal_scroll_viewport(m_impl->terminal, behavior);
}

std::expected<TerminalSearchResult, std::error_code>
GhosttyTerminalEngine::search(const std::string_view query, const TerminalSearchDirection direction,
                              const bool caseSensitive)
{
    if (query.empty())
    {
        if (const std::error_code error = clearSearch())
        {
            return std::unexpected(error);
        }
        return TerminalSearchResult{};
    }

    struct SearchCell
    {
        GhosttyGridRef ref{};
        std::uint32_t screenRow = 0;
        std::uint16_t column = 0;
    };
    struct Match
    {
        SearchCell start;
        SearchCell end;
    };

    std::uint16_t columns = 0;
    std::size_t totalRows = 0;
    const std::array dataKeys = {GHOSTTY_TERMINAL_DATA_COLS, GHOSTTY_TERMINAL_DATA_TOTAL_ROWS};
    std::array<void *, 2> dataValues = {&columns, &totalRows};
    if (const GhosttyResult result =
            ghostty_terminal_get_multi(m_impl->terminal, dataKeys.size(), dataKeys.data(), dataValues.data(), nullptr);
        result != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(result));
    }
    if (columns == 0 || totalRows == 0 || totalRows > std::numeric_limits<std::uint32_t>::max())
    {
        return TerminalSearchResult{};
    }

    const std::string needle = caseSensitive ? std::string(query) : asciiFold(std::string(query));
    std::vector<Match> matches;
    std::string logicalLine;
    std::vector<SearchCell> byteCells;

    const auto collectMatches = [&]() {
        const std::string haystack = caseSensitive ? logicalLine : asciiFold(logicalLine);
        std::size_t offset = 0;
        while ((offset = haystack.find(needle, offset)) != std::string::npos)
        {
            const std::size_t endOffset = offset + needle.size() - 1U;
            if (endOffset < byteCells.size())
            {
                matches.push_back({.start = byteCells[offset], .end = byteCells[endOffset]});
            }
            offset += std::max<std::size_t>(needle.size(), 1U);
        }
        logicalLine.clear();
        byteCells.clear();
    };

    for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(totalRows); ++row)
    {
        bool wrapped = false;
        for (std::uint16_t column = 0; column < columns; ++column)
        {
            GhosttyPoint point{};
            point.tag = GHOSTTY_POINT_TAG_SCREEN;
            point.value.coordinate = {.x = column, .y = row};

            GhosttyGridRef ref{};
            if (const GhosttyResult refResult = ghostty_terminal_grid_ref(m_impl->terminal, point, &ref);
                refResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(refResult));
            }

            if (column == 0)
            {
                GhosttyRow ghosttyRow{};
                if (const GhosttyResult rowResult = ghostty_grid_ref_row(&ref, &ghosttyRow);
                    rowResult != GHOSTTY_SUCCESS)
                {
                    return std::unexpected(ghosttyError(rowResult));
                }
                if (const GhosttyResult wrapResult = ghostty_row_get(ghosttyRow, GHOSTTY_ROW_DATA_WRAP, &wrapped);
                    wrapResult != GHOSTTY_SUCCESS)
                {
                    return std::unexpected(ghosttyError(wrapResult));
                }
            }

            GhosttyCell cell{};
            if (const GhosttyResult cellResult = ghostty_grid_ref_cell(&ref, &cell); cellResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(cellResult));
            }
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            if (const GhosttyResult wideResult = ghostty_cell_get(cell, GHOSTTY_CELL_DATA_WIDE, &wide);
                wideResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(wideResult));
            }
            if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD)
            {
                continue;
            }

            std::size_t graphemeLength = 0;
            const GhosttyResult lengthResult = ghostty_grid_ref_graphemes(&ref, nullptr, 0, &graphemeLength);
            if (lengthResult != GHOSTTY_OUT_OF_SPACE && lengthResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(lengthResult));
            }

            std::string cellText;
            if (graphemeLength == 0)
            {
                cellText = " ";
            }
            else
            {
                std::vector<std::uint32_t> codepoints(graphemeLength);
                std::size_t written = 0;
                if (const GhosttyResult graphemeResult =
                        ghostty_grid_ref_graphemes(&ref, codepoints.data(), codepoints.size(), &written);
                    graphemeResult != GHOSTTY_SUCCESS)
                {
                    return std::unexpected(ghosttyError(graphemeResult));
                }
                for (std::size_t index = 0; index < written; ++index)
                {
                    appendUtf8(cellText, codepoints[index]);
                }
            }

            logicalLine.append(cellText);
            byteCells.insert(byteCells.end(), cellText.size(),
                             SearchCell{.ref = ref, .screenRow = row, .column = column});
        }
        if (!wrapped)
        {
            collectMatches();
        }
    }
    if (!logicalLine.empty())
    {
        collectMatches();
    }

    if (matches.empty())
    {
        if (const std::error_code error = clearSearch())
        {
            return std::unexpected(error);
        }
        m_impl->lastSearchQuery = query;
        m_impl->lastSearchCaseSensitive = caseSensitive;
        return TerminalSearchResult{};
    }

    const bool continuing = m_impl->lastSearchQuery == query && m_impl->lastSearchCaseSensitive == caseSensitive;
    std::optional<GhosttyPointCoordinate> activeStart;
    if (continuing)
    {
        GhosttySelection selection{};
        selection.size = sizeof(selection);
        if (ghostty_terminal_get(m_impl->terminal, GHOSTTY_TERMINAL_DATA_SELECTION, &selection) == GHOSTTY_SUCCESS)
        {
            GhosttyPointCoordinate point{};
            if (ghostty_terminal_point_from_grid_ref(m_impl->terminal, &selection.start, GHOSTTY_POINT_TAG_SCREEN,
                                                     &point)
                == GHOSTTY_SUCCESS)
            {
                activeStart = point;
            }
        }
    }

    std::size_t selectedIndex = direction == TerminalSearchDirection::forward ? 0 : matches.size() - 1U;
    bool wrapped = false;
    if (activeStart)
    {
        const auto sameStart = [&activeStart](const Match &match) {
            return match.start.screenRow == activeStart->y && match.start.column == activeStart->x;
        };
        const auto current = std::ranges::find_if(matches, sameStart);
        if (current != matches.end())
        {
            const auto currentIndex = static_cast<std::size_t>(std::distance(matches.begin(), current));
            if (direction == TerminalSearchDirection::forward)
            {
                selectedIndex = currentIndex + 1U;
                if (selectedIndex == matches.size())
                {
                    selectedIndex = 0;
                    wrapped = true;
                }
            }
            else if (currentIndex == 0)
            {
                selectedIndex = matches.size() - 1U;
                wrapped = true;
            }
            else
            {
                selectedIndex = currentIndex - 1U;
            }
        }
    }

    GhosttySelection selection{};
    selection.size = sizeof(selection);
    selection.start = matches[selectedIndex].start.ref;
    selection.end = matches[selectedIndex].end.ref;
    if (const GhosttyResult selectionResult =
            ghostty_terminal_set(m_impl->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &selection);
        selectionResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(selectionResult));
    }

    GhosttyTerminalScrollbar scrollbar{};
    if (const GhosttyResult scrollbarResult =
            ghostty_terminal_get(m_impl->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);
        scrollbarResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(scrollbarResult));
    }
    const std::uint64_t maximumOffset = scrollbar.total > scrollbar.len ? scrollbar.total - scrollbar.len : 0;
    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
    behavior.value.row =
        static_cast<std::size_t>(std::min<std::uint64_t>(matches[selectedIndex].start.screenRow, maximumOffset));
    ghostty_terminal_scroll_viewport(m_impl->terminal, behavior);

    m_impl->lastSearchQuery = query;
    m_impl->lastSearchCaseSensitive = caseSensitive;
    return TerminalSearchResult{.current = static_cast<std::uint32_t>(selectedIndex + 1U),
                                .total = static_cast<std::uint32_t>(
                                    std::min<std::size_t>(matches.size(), std::numeric_limits<std::uint32_t>::max())),
                                .wrapped = wrapped};
}

std::error_code GhosttyTerminalEngine::clearSearch()
{
    m_impl->lastSearchQuery.clear();
    const GhosttyResult result = ghostty_terminal_set(m_impl->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
    return result == GHOSTTY_SUCCESS ? std::error_code{} : ghosttyError(result);
}

std::expected<std::vector<std::byte>, std::error_code>
GhosttyTerminalEngine::encodePaste(const std::span<const std::byte> bytes) const
{
    std::vector<char> input(bytes.size());
    if (!bytes.empty())
    {
        std::memcpy(input.data(), bytes.data(), bytes.size());
    }

    bool bracketed = false;
    if (const GhosttyResult modeResult =
            ghostty_terminal_mode_get(m_impl->terminal, GHOSTTY_MODE_BRACKETED_PASTE, &bracketed);
        modeResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(modeResult));
    }

    std::vector<std::byte> result(bytes.size() + 12U);
    std::size_t written = 0;
    if (const GhosttyResult pasteResult = ghostty_paste_encode(
            input.data(), input.size(), bracketed, reinterpret_cast<char *>(result.data()), result.size(), &written);
        pasteResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(pasteResult));
    }
    result.resize(written);
    return result;
}

std::expected<TerminalSnapshot, std::error_code> GhosttyTerminalEngine::snapshot()
{
    if (const GhosttyResult updateResult = ghostty_render_state_update(m_impl->renderState, m_impl->terminal);
        updateResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(updateResult));
    }

    TerminalSnapshot result;
    GhosttyRenderStateDirty dirtyState = GHOSTTY_RENDER_STATE_DIRTY_FULL;
    if (const GhosttyResult dirtyResult =
            ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirtyState);
        dirtyResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(dirtyResult));
    }
    switch (dirtyState)
    {
        case GHOSTTY_RENDER_STATE_DIRTY_FALSE:
            result.damage = TerminalDamageKind::none;
            break;
        case GHOSTTY_RENDER_STATE_DIRTY_PARTIAL:
            result.damage = TerminalDamageKind::partial;
            break;
        case GHOSTTY_RENDER_STATE_DIRTY_FULL:
        default:
            result.damage = TerminalDamageKind::full;
            break;
    }

    if (const GhosttyResult colsResult =
            ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_COLS, &result.columns);
        colsResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(colsResult));
    }
    if (const GhosttyResult rowsResult =
            ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_ROWS, &result.rows);
        rowsResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(rowsResult));
    }

    GhosttyRenderStateColors colors{};
    colors.size = sizeof(colors);
    if (const GhosttyResult colorResult = ghostty_render_state_colors_get(m_impl->renderState, &colors);
        colorResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(colorResult));
    }
    result.defaultForeground = terminalColor(colors.foreground);
    result.defaultBackground = terminalColor(colors.background);
    result.cursor.color = terminalColor(colors.cursor_has_value ? colors.cursor : colors.foreground);

    GhosttyTerminalScrollbar scrollbar{};
    if (const GhosttyResult scrollbarResult =
            ghostty_terminal_get(m_impl->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);
        scrollbarResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(scrollbarResult));
    }
    result.scrollbar = {.total = scrollbar.total, .offset = scrollbar.offset, .visible = scrollbar.len};

    bool cursorInViewport = false;
    bool cursorVisible = false;
    ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
                             &cursorInViewport);
    ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursorVisible);
    result.cursor.visible = cursorInViewport && cursorVisible;
    ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &result.cursor.blinking);
    if (cursorInViewport)
    {
        ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X,
                                 &result.cursor.column);
        ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &result.cursor.row);
        bool cursorWideTail = false;
        ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL,
                                 &cursorWideTail);
        if (cursorWideTail && result.cursor.column > 0)
        {
            --result.cursor.column;
            result.cursor.width = 2;
        }
    }

    GhosttyRenderStateCursorVisualStyle cursorStyle = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
    ghostty_render_state_get(m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &cursorStyle);
    switch (cursorStyle)
    {
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
            result.cursor.style = TerminalCursorStyle::bar;
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
            result.cursor.style = TerminalCursorStyle::underline;
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
            result.cursor.style = TerminalCursorStyle::hollowBlock;
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK:
        default:
            result.cursor.style = TerminalCursorStyle::block;
            break;
    }

    result.cells.reserve(static_cast<std::size_t>(result.columns) * result.rows);
    if (const GhosttyResult iteratorResult = ghostty_render_state_get(
            m_impl->renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, static_cast<void *>(&m_impl->rowIterator));
        iteratorResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(iteratorResult));
    }

    std::uint16_t row = 0;
    while (ghostty_render_state_row_iterator_next(m_impl->rowIterator))
    {
        bool rowDirty = false;
        if (const GhosttyResult dirtyResult =
                ghostty_render_state_row_get(m_impl->rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY, &rowDirty);
            dirtyResult != GHOSTTY_SUCCESS)
        {
            return std::unexpected(ghosttyError(dirtyResult));
        }
        if (result.damage == TerminalDamageKind::partial && rowDirty)
        {
            result.damagedRows.push_back(row);
        }

        GhosttyRenderStateRowSelection rowSelection{};
        rowSelection.size = sizeof(rowSelection);
        const GhosttyResult selectionResult =
            ghostty_render_state_row_get(m_impl->rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &rowSelection);
        if (selectionResult != GHOSTTY_SUCCESS && selectionResult != GHOSTTY_NO_VALUE)
        {
            return std::unexpected(ghosttyError(selectionResult));
        }

        if (const GhosttyResult cellsResult = ghostty_render_state_row_get(
                m_impl->rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, static_cast<void *>(&m_impl->rowCells));
            cellsResult != GHOSTTY_SUCCESS)
        {
            return std::unexpected(ghosttyError(cellsResult));
        }

        std::uint16_t column = 0;
        while (ghostty_render_state_row_cells_next(m_impl->rowCells))
        {
            TerminalCell cell;
            cell.foreground = result.defaultForeground;
            cell.background = result.defaultBackground;

            GhosttyCell rawCell{};
            if (const GhosttyResult rawResult = ghostty_render_state_row_cells_get(
                    m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &rawCell);
                rawResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(rawResult));
            }
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            if (const GhosttyResult wideResult = ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_WIDE, &wide);
                wideResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(wideResult));
            }
            switch (wide)
            {
                case GHOSTTY_CELL_WIDE_WIDE:
                    cell.displayWidth = 2;
                    break;
                case GHOSTTY_CELL_WIDE_SPACER_TAIL:
                case GHOSTTY_CELL_WIDE_SPACER_HEAD:
                    cell.displayWidth = 0;
                    break;
                case GHOSTTY_CELL_WIDE_NARROW:
                default:
                    cell.displayWidth = 1;
                    break;
            }

            GhosttyColorRgb foreground{};
            if (ghostty_render_state_row_cells_get(m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR,
                                                   &foreground)
                == GHOSTTY_SUCCESS)
            {
                cell.foreground = terminalColor(foreground);
            }

            GhosttyColorRgb background{};
            if (ghostty_render_state_row_cells_get(m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                                                   &background)
                == GHOSTTY_SUCCESS)
            {
                cell.background = terminalColor(background);
            }

            GhosttyStyle style{};
            style.size = sizeof(style);
            if (const GhosttyResult styleResult = ghostty_render_state_row_cells_get(
                    m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
                styleResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(styleResult));
            }
            cell.bold = style.bold;
            cell.italic = style.italic;
            cell.underline = style.underline != 0;
            cell.strikethrough = style.strikethrough;
            cell.overline = style.overline;
            cell.invisible = style.invisible;
            cell.selected =
                selectionResult == GHOSTTY_SUCCESS && column >= rowSelection.start_x && column <= rowSelection.end_x;
            if (style.inverse)
            {
                std::swap(cell.foreground, cell.background);
            }

            std::uint32_t graphemeLength = 0;
            if (const GhosttyResult lengthResult = ghostty_render_state_row_cells_get(
                    m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &graphemeLength);
                lengthResult != GHOSTTY_SUCCESS)
            {
                return std::unexpected(ghosttyError(lengthResult));
            }
            if (graphemeLength > 0)
            {
                cell.grapheme.resize(graphemeLength);
                if (const GhosttyResult graphemeResult = ghostty_render_state_row_cells_get(
                        m_impl->rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cell.grapheme.data());
                    graphemeResult != GHOSTTY_SUCCESS)
                {
                    return std::unexpected(ghosttyError(graphemeResult));
                }
            }
            result.cells.push_back(std::move(cell));
            ++column;
        }

        constexpr bool clean = false;
        if (const GhosttyResult cleanResult =
                ghostty_render_state_row_set(m_impl->rowIterator, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
            cleanResult != GHOSTTY_SUCCESS)
        {
            return std::unexpected(ghosttyError(cleanResult));
        }
        ++row;
    }

    const std::size_t expectedCells = static_cast<std::size_t>(result.columns) * result.rows;
    result.cells.resize(expectedCells,
                        TerminalCell{.foreground = result.defaultForeground, .background = result.defaultBackground});
    if (result.cursor.width == 1 && result.cursor.column < result.columns && result.cursor.row < result.rows)
    {
        result.cursor.width =
            std::max<std::uint8_t>(1, result.cell(result.cursor.column, result.cursor.row).displayWidth);
    }

    constexpr GhosttyRenderStateDirty cleanState = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    if (const GhosttyResult cleanResult =
            ghostty_render_state_set(m_impl->renderState, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &cleanState);
        cleanResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(cleanResult));
    }
    return result;
}

std::expected<std::string, std::error_code> GhosttyTerminalEngine::plainText() const
{
    GhosttyFormatterTerminalOptions options{};
    options.size = sizeof(options);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = true;
    options.trim = true;

    UniqueFormatter formatter;
    const GhosttyResult createResult =
        ghostty_formatter_terminal_new(nullptr, &formatter.handle, m_impl->terminal, options);
    if (createResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(createResult));
    }

    GhosttyOwnedBuffer buffer;
    const GhosttyResult formatResult =
        ghostty_formatter_format_alloc(formatter.handle, nullptr, &buffer.data, &buffer.length);
    if (formatResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(formatResult));
    }

    if (buffer.length == 0)
    {
        return std::string{};
    }
    return std::string(reinterpret_cast<const char *>(buffer.data), buffer.length);
}

} // namespace ztermy::terminal
