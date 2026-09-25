#include "domain/terminal/SixelDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ztermy::terminal
{
namespace
{
constexpr std::uint32_t maximumDimension = 8192;
constexpr std::uint64_t maximumPixels = std::uint64_t{8} * 1024 * 1024;
constexpr std::size_t maximumInput = std::size_t{8} * 1024 * 1024;
constexpr std::uint64_t maximumPaintWork = std::uint64_t{64} * 1024 * 1024;
constexpr std::uint16_t emptyPixel = 256;

std::uint8_t channel(std::uint32_t percent)
{
    return static_cast<std::uint8_t>((percent * 255 + 50) / 100);
}

TerminalColor hlsColor(std::uint32_t hue, std::uint32_t lightness, std::uint32_t saturation)
{
    const double h = static_cast<double>((hue + 240) % 360) / 60.0;
    const double l = lightness / 100.0;
    const double s = saturation / 100.0;
    const double chroma = (1 - std::abs(2 * l - 1)) * s;
    const double x = chroma * (1 - std::abs(std::fmod(h, 2) - 1));
    const double m = l - chroma / 2;
    const std::array<std::array<double, 3>, 6> sectors{
        {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x}, {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}}};
    const auto &rgb = sectors[static_cast<std::size_t>(h)];
    return {.red = static_cast<std::uint8_t>(std::lround((rgb[0] + m) * 255)),
            .green = static_cast<std::uint8_t>(std::lround((rgb[1] + m) * 255)),
            .blue = static_cast<std::uint8_t>(std::lround((rgb[2] + m) * 255))};
}
} // namespace

SixelPalette defaultSixelPalette()
{
    constexpr std::array<std::array<std::uint32_t, 3>, 16> colors{{{0, 0, 0},
                                                                   {20, 20, 80},
                                                                   {80, 13, 13},
                                                                   {20, 80, 20},
                                                                   {80, 20, 80},
                                                                   {20, 80, 80},
                                                                   {80, 80, 20},
                                                                   {53, 53, 53},
                                                                   {26, 26, 26},
                                                                   {33, 33, 60},
                                                                   {60, 26, 26},
                                                                   {33, 60, 33},
                                                                   {60, 33, 60},
                                                                   {33, 60, 60},
                                                                   {60, 60, 33},
                                                                   {80, 80, 80}}};
    SixelPalette palette{};
    for (std::size_t i = 0; i < colors.size(); ++i)
        palette[i] = {.red = channel(colors[i][0]), .green = channel(colors[i][1]), .blue = channel(colors[i][2])};
    return palette;
}

SixelDecoder::SixelDecoder(SixelOptions options) : m_options(options) {}

bool SixelDecoder::fail(SixelError error)
{
    m_failed = true;
    m_error = error;
    std::vector<std::uint16_t>().swap(m_pixels);
    return false;
}

bool SixelDecoder::ensureSize(std::uint32_t width, std::uint32_t height)
{
    if (width > maximumDimension || height > maximumDimension || std::uint64_t{width} * height > maximumPixels)
        return fail(SixelError::limit);
    m_width = std::max(m_width, width);
    m_height = std::max(m_height, height);
    if (std::uint64_t{m_width} * m_height > maximumPixels)
        return fail(SixelError::limit);
    if (m_width <= m_stride && m_height <= m_allocatedRows)
        return true;
    auto stride = m_width > m_stride ? std::min(maximumDimension, std::max({m_width, std::uint32_t{16}, m_stride * 2}))
                                     : m_stride;
    auto rows = m_height > m_allocatedRows
                    ? std::min(maximumDimension, std::max({m_height, std::uint32_t{16}, m_allocatedRows * 2}))
                    : m_allocatedRows;
    if (std::uint64_t{stride} * rows > maximumPixels)
    {
        stride = m_width;
        rows = m_height;
    }
    const auto allocationWork = std::uint64_t{stride} * rows;
    if (allocationWork > maximumPaintWork - m_resizeWork)
        return fail(SixelError::limit);
    m_resizeWork += allocationWork;
    std::vector<std::uint16_t> expanded(static_cast<std::size_t>(stride) * rows, emptyPixel);
    for (std::uint32_t row = 0; row < std::min(m_allocatedRows, rows); ++row)
        std::copy_n(m_pixels.begin() + static_cast<std::ptrdiff_t>(row) * m_stride, std::min(m_stride, stride),
                    expanded.begin() + static_cast<std::ptrdiff_t>(row) * stride);
    m_pixels = std::move(expanded);
    m_stride = stride;
    m_allocatedRows = rows;
    return true;
}

bool SixelDecoder::draw(std::uint8_t bits, std::uint32_t count)
{
    count = std::max(std::uint32_t{1}, count);
    if (count > maximumDimension - m_x || m_y > maximumDimension - 6)
        return fail(SixelError::limit);
    const auto work = std::uint64_t{count} * 6;
    if (work > maximumPaintWork - m_paintWork)
        return fail(SixelError::limit);
    m_paintWork += work;
    if (!ensureSize(m_x + count, m_y + 6))
        return false;
    for (std::uint32_t bit = 0; bit < 6; ++bit)
        if ((bits & (1U << bit)) != 0)
            std::fill_n(m_pixels.begin() + static_cast<std::ptrdiff_t>(m_y + bit) * m_stride + m_x, count,
                        static_cast<std::uint16_t>(m_color));
    m_x += count;
    m_drew = true;
    return true;
}

bool SixelDecoder::applyParameters()
{
    const auto &p = m_parameters;
    if (m_command == '"')
    {
        if (m_drew || m_parameterIndex > 3 || p[0] > maximumDimension || p[1] > maximumDimension)
            return fail(SixelError::invalid);
        m_options.aspectNumerator = std::max(std::uint32_t{1}, p[0]);
        m_options.aspectDenominator = std::max(std::uint32_t{1}, p[1]);
        return ensureSize(p[2], p[3]);
    }
    if (p[0] >= m_options.palette.size())
        return fail(SixelError::invalid);
    m_color = p[0];
    if (m_parameterIndex == 0)
        return true;
    if (m_parameterIndex != 4 || p[3] > 100 || p[4] > 100)
        return fail(SixelError::invalid);
    if (p[1] == 2 && p[2] <= 100)
        m_options.palette[m_color] = {.red = channel(p[2]), .green = channel(p[3]), .blue = channel(p[4])};
    else if (p[1] == 1 && p[2] <= 360)
        m_options.palette[m_color] = hlsColor(p[2], p[3], p[4]);
    else
        return fail(SixelError::invalid);
    return true;
}

bool SixelDecoder::consume(char byte)
{
    if (byte == '\r' || byte == '\n' || byte == '\t')
        return true;
    if (m_state != State::data)
    {
        if (byte >= '0' && byte <= '9')
        {
            auto &number = m_parameters[m_parameterIndex];
            const auto digit = static_cast<std::uint32_t>(byte - '0');
            if (number > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
                return fail(SixelError::limit);
            number = number * 10 + digit;
            return true;
        }
        if (byte == ';' && m_state == State::parameters)
        {
            if (++m_parameterIndex >= m_parameters.size())
                return fail(SixelError::invalid);
            return true;
        }
        if (m_state == State::repeat)
        {
            m_state = State::data;
            return byte >= '?' && byte <= '~' ? draw(static_cast<std::uint8_t>(byte - '?'), m_parameters[0])
                                              : fail(SixelError::invalid);
        }
        if (!applyParameters())
            return false;
        m_state = State::data;
    }
    if (byte >= '?' && byte <= '~')
        return draw(static_cast<std::uint8_t>(byte - '?'), 1);
    if (byte == '#' || byte == '"' || byte == '!')
    {
        m_command = byte;
        m_parameters = {};
        m_parameterIndex = 0;
        m_state = byte == '!' ? State::repeat : State::parameters;
        return true;
    }
    if (byte == '$')
        m_x = 0;
    else if (byte == '-')
    {
        if (m_y > maximumDimension - 6)
            return fail(SixelError::limit);
        m_x = 0;
        m_y += 6;
    }
    else
        return fail(SixelError::invalid);
    return true;
}

bool SixelDecoder::append(std::string_view bytes)
{
    if (m_failed || m_finished)
        return false;
    if (bytes.size() > maximumInput - m_inputBytes)
        return fail(SixelError::limit);
    m_inputBytes += bytes.size();
    try
    {
        for (const auto byte : bytes)
            if (!consume(byte))
                return false;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return fail(SixelError::allocation);
    }
}

std::expected<SixelImage, SixelError> SixelDecoder::finish()
{
    if (m_finished)
        return std::unexpected(SixelError::invalid);
    m_finished = true;
    try
    {
        if (!m_failed && m_state == State::parameters)
            applyParameters();
        if (m_failed)
            return std::unexpected(m_error);
        if (m_state == State::repeat || !m_width || !m_height)
            return std::unexpected(SixelError::invalid);
        SixelImage result{.options = m_options, .cursorX = m_x, .cursorY = m_y};
        result.image.width = m_width;
        result.image.height = m_height;
        result.image.pixels.resize(static_cast<std::size_t>(m_width) * m_height * 4);
        for (std::uint32_t y = 0; y < m_height; ++y)
            for (std::uint32_t x = 0; x < m_width; ++x)
            {
                const auto index = m_pixels[static_cast<std::size_t>(y) * m_stride + x];
                const auto color = index == emptyPixel ? m_options.background : m_options.palette[index];
                const auto offset = (static_cast<std::size_t>(y) * m_width + x) * 4;
                result.image.pixels[offset] = color.red;
                result.image.pixels[offset + 1] = color.green;
                result.image.pixels[offset + 2] = color.blue;
                result.image.pixels[offset + 3] = index == emptyPixel && m_options.transparent ? 0 : 255;
            }
        std::vector<std::uint16_t>().swap(m_pixels);
        return result;
    }
    catch (const std::bad_alloc &)
    {
        fail(SixelError::allocation);
        return std::unexpected(m_error);
    }
}
} // namespace ztermy::terminal
