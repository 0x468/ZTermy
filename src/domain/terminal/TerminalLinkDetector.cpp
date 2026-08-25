#include "domain/terminal/TerminalLinkDetector.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace ztermy::terminal
{
namespace
{

[[nodiscard]] bool schemeAt(const std::string_view text, const std::size_t offset, const std::string_view scheme)
{
    if (offset + scheme.size() > text.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < scheme.size(); ++index)
    {
        const auto value = static_cast<unsigned char>(text[offset + index]);
        if (static_cast<char>(std::tolower(value)) != scheme[index])
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool startsUrl(const std::string_view text, const std::size_t offset)
{
    if (offset > 0)
    {
        const auto previous = static_cast<unsigned char>(text[offset - 1]);
        if (std::isalnum(previous) != 0 || previous == '_' || previous == '-')
        {
            return false;
        }
    }
    return schemeAt(text, offset, "https://") || schemeAt(text, offset, "http://");
}

[[nodiscard]] bool terminatesUrl(const char value)
{
    const auto byte = static_cast<unsigned char>(value);
    return std::isspace(byte) != 0 || std::iscntrl(byte) != 0 || value == '<' || value == '>' || value == '"'
           || value == '\'' || value == '`';
}

[[nodiscard]] bool trailingPunctuation(const char value)
{
    return value == '.' || value == ',' || value == ';' || value == ':' || value == '!' || value == '?' || value == ')'
           || value == ']' || value == '}';
}

struct RowText final
{
    std::string text;
    std::vector<std::uint16_t> columns;
};

[[nodiscard]] RowText terminalRowText(const TerminalSnapshot &snapshot, const std::uint16_t row)
{
    RowText result;
    result.text.reserve(snapshot.columns);
    result.columns.reserve(snapshot.columns);
    for (std::uint16_t column = 0; column < snapshot.columns; ++column)
    {
        const TerminalCell &cell = snapshot.cell(column, row);
        if (cell.displayWidth == 0)
        {
            continue;
        }
        if (cell.grapheme.empty())
        {
            result.text.push_back(' ');
            result.columns.push_back(column);
            continue;
        }
        for (const char32_t codepoint : cell.grapheme)
        {
            result.text.push_back(codepoint >= 0x20 && codepoint <= 0x7e ? static_cast<char>(codepoint) : ' ');
            result.columns.push_back(column);
        }
    }
    return result;
}

} // namespace

std::uint32_t internTerminalHyperlink(TerminalSnapshot &snapshot, std::string uri, const TerminalHyperlinkKind kind)
{
    if (uri.empty() || uri.size() > maximumTerminalHyperlinkUriBytes)
    {
        return 0;
    }
    const auto existing = std::ranges::find_if(snapshot.hyperlinks, [&uri, kind](const TerminalHyperlink &link) {
        return link.kind == kind && link.uri == uri;
    });
    if (existing != snapshot.hyperlinks.end())
    {
        return static_cast<std::uint32_t>(std::distance(snapshot.hyperlinks.begin(), existing) + 1);
    }
    if (snapshot.hyperlinks.size() >= maximumTerminalHyperlinksPerSnapshot)
    {
        return 0;
    }
    snapshot.hyperlinks.push_back(TerminalHyperlink{.uri = std::move(uri), .kind = kind});
    return static_cast<std::uint32_t>(snapshot.hyperlinks.size());
}

void detectAutomaticTerminalLinks(TerminalSnapshot &snapshot)
{
    if (snapshot.columns == 0 || snapshot.rows == 0 || snapshot.cells.empty())
    {
        return;
    }

    std::size_t matchCount = 0;
    for (std::uint16_t row = 0; row < snapshot.rows && matchCount < maximumAutomaticLinksPerSnapshot; ++row)
    {
        const RowText rowText = terminalRowText(snapshot, row);
        for (std::size_t start = 0; start < rowText.text.size() && matchCount < maximumAutomaticLinksPerSnapshot;)
        {
            if (!startsUrl(rowText.text, start))
            {
                ++start;
                continue;
            }
            std::size_t end = start;
            while (end < rowText.text.size() && !terminatesUrl(rowText.text[end]))
            {
                ++end;
            }
            while (end > start && trailingPunctuation(rowText.text[end - 1]))
            {
                --end;
            }
            const std::size_t schemeLength = schemeAt(rowText.text, start, "https://") ? 8 : 7;
            if (end <= start + schemeLength || end - start > maximumTerminalHyperlinkUriBytes)
            {
                start = std::max(start + 1, end);
                continue;
            }

            const std::uint16_t firstColumn = rowText.columns[start];
            const std::uint16_t finalColumn = rowText.columns[end - 1];
            bool claimed = false;
            for (std::uint16_t column = firstColumn; column <= finalColumn; ++column)
            {
                if (snapshot.cell(column, row).hyperlinkId != 0)
                {
                    claimed = true;
                    break;
                }
            }
            if (!claimed)
            {
                const std::uint32_t id = internTerminalHyperlink(snapshot, rowText.text.substr(start, end - start),
                                                                 TerminalHyperlinkKind::automaticUrl);
                if (id != 0)
                {
                    for (std::uint16_t column = firstColumn; column <= finalColumn; ++column)
                    {
                        snapshot.cell(column, row).hyperlinkId = id;
                    }
                    ++matchCount;
                }
            }
            start = std::max(start + 1, end);
        }
    }
}

} // namespace ztermy::terminal
