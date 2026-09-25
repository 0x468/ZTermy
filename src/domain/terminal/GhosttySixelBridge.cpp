#include "domain/terminal/GhosttySixelBridge.h"
#include "domain/terminal/GhosttyImageExtension.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>

namespace ztermy::terminal
{
namespace
{
bool nextNumber(std::string_view &text, std::uint32_t &value)
{
    const auto separator = text.find(';');
    const auto token = text.substr(0, separator);
    value = 0;
    if (!token.empty())
    {
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
            return false;
    }
    text = separator == std::string_view::npos ? std::string_view{} : text.substr(separator + 1);
    return true;
}
} // namespace

void GhosttySixelBridge::feed(std::string_view bytes, TerminalGeometry geometry)
{
    m_geometry = geometry;
    m_stream.append(bytes, *this);
}

void GhosttySixelBridge::writeTerminal(std::string_view bytes)
{
    ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
}

void GhosttySixelBridge::beginSixel(std::string_view parameters)
{
    m_decoder.reset();
    if (!m_geometry.cellWidthPixels || !m_geometry.cellHeightPixels)
        return;
    std::array<std::uint32_t, 3> values{};
    for (auto &value : values)
        if (!nextNumber(parameters, value))
            return;
    if (!parameters.empty())
        return;
    SixelOptions options;
    options.transparent = values[1] == 1;
    constexpr std::array<std::uint32_t, 10> aspects{2, 2, 5, 3, 3, 2, 2, 1, 1, 1};
    options.aspectNumerator = values[0] < aspects.size() ? aspects[values[0]] : 2;
    GhosttyColorRgb background{};
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND, &background) == GHOSTTY_SUCCESS)
        options.background = {.red = background.r, .green = background.g, .blue = background.b};
    m_decoder.emplace(options);
}

void GhosttySixelBridge::writeSixel(std::string_view bytes)
{
    if (m_decoder && !m_decoder->append(bytes))
        m_decoder.reset();
}

void GhosttySixelBridge::endSixel(bool cancelled)
{
    if (m_decoder && !cancelled)
    {
        const auto image = m_decoder->finish();
        if (image)
            display(*image);
    }
    m_decoder.reset();
}

void GhosttySixelBridge::display(const SixelImage &decoded)
{
    const auto &image = decoded.image;
    const auto numerator = std::uint64_t{decoded.options.aspectNumerator};
    const auto denominator = std::max(decoded.options.aspectDenominator, 1U);
    const auto height = (image.height * numerator + denominator - 1) / denominator;
    const auto bytes = height * image.width * 4;
    if (!height || height > 8192 || bytes > std::uint64_t{32} * 1024U * 1024U)
        return;

    if (ztermy_ghostty_insert_image(m_terminal, image.pixels.data(), image.pixels.size(), image.width, image.height,
                                    decoded.options.aspectNumerator, denominator, m_absolute)
            != GHOSTTY_SUCCESS
        || m_absolute)
        return;
    const auto rows = (height + m_geometry.cellHeightPixels - 1) / m_geometry.cellHeightPixels;
    const auto advance = m_cursorRight ? rows - 1 : rows;
    for (std::uint64_t row = 0; row < advance; ++row)
        writeTerminal("\x1b"
                      "D");
    if (m_cursorRight)
    {
        const auto columns = (image.width + std::uint64_t{m_geometry.cellWidthPixels} - 1) / m_geometry.cellWidthPixels;
        writeTerminal("\x1b[" + std::to_string(columns) + "C");
    }
}

void GhosttySixelBridge::controlSequence(std::string_view parameters)
{
    if (parameters.starts_with('?') && parameters.ends_with('S'))
    {
        graphicsQuery(parameters.substr(1, parameters.size() - 2));
        return;
    }
    if (parameters == "!p")
    {
        resetTerminal();
        return;
    }
    if (parameters.size() < 3 || parameters.front() != '?')
        return;
    const char action = parameters.back();
    if (action != 'h' && action != 'l' && action != 's' && action != 'r')
        return;
    parameters.remove_prefix(1);
    parameters.remove_suffix(1);
    while (!parameters.empty())
    {
        std::uint32_t mode = 0;
        if (!nextNumber(parameters, mode))
            return;
        if (mode != 80 && mode != 8452)
            continue;
        auto &current = mode == 80 ? m_absolute : m_cursorRight;
        auto &saved = mode == 80 ? m_savedAbsolute : m_savedCursorRight;
        if (action == 's')
            saved = current;
        else
            current = action == 'r' ? saved : action == 'h';
    }
}

void GhosttySixelBridge::resetTerminal()
{
    m_absolute = m_cursorRight = m_savedAbsolute = m_savedCursorRight = false;
    m_decoder.reset();
}

void GhosttySixelBridge::graphicsQuery(std::string_view parameters)
{
    std::array<std::uint32_t, 4> values{};
    for (auto &value : values)
        if (!nextNumber(parameters, value))
            return;
    if (!parameters.empty())
        return;
    const auto item = values[0];
    const auto action = values[1];
    unsigned status = 0;
    std::string value;
    if (item != 1 && item != 2)
        status = 1; // ReGIS is not implemented.
    else if (action == 2 || action == 3)
        status = 3; // Resource-policy attributes are read-only.
    else if (action != 1 && action != 4)
        status = 2;
    else if (item == 1)
        value = ";256";
    else
    {
        const auto width =
            action == 4 ? 8192
                        : std::min(std::uint64_t{m_geometry.columns} * m_geometry.cellWidthPixels, std::uint64_t{8192});
        const auto height =
            action == 4 ? 8192
                        : std::min(std::uint64_t{m_geometry.rows} * m_geometry.cellHeightPixels, std::uint64_t{8192});
        value = ";" + std::to_string(width) + ";" + std::to_string(height);
    }
    const auto response = "\x1b[?" + std::to_string(item) + ";" + std::to_string(status) + value + "S";
    if (m_reply)
        m_reply(m_terminal, m_userdata, reinterpret_cast<const std::uint8_t *>(response.data()), response.size());
}
} // namespace ztermy::terminal
