#pragma once

#include "core/config/TerminalColorScheme.h"
#include "domain/terminal/TerminalImage.h"

#include <array>
#include <expected>
#include <string_view>

namespace ztermy::terminal
{
using SixelPalette = std::array<TerminalColor, 256>;
[[nodiscard]] SixelPalette defaultSixelPalette();

struct SixelOptions
{
    bool transparent = false;
    TerminalColor background;
    std::uint32_t aspectNumerator = 2;
    std::uint32_t aspectDenominator = 1;
    SixelPalette palette = defaultSixelPalette();
};

struct SixelImage
{
    TerminalImage image;
    SixelOptions options;
    std::uint32_t cursorX = 0;
    std::uint32_t cursorY = 0;
};

enum class SixelError : std::uint8_t
{
    invalid,
    limit,
    allocation
};

// Streaming decoder for bytes after DCS ... q, excluding the string terminator.
// Framing, cancellation, terminal cursor movement and storage belong to the caller.
class SixelDecoder final
{
public:
    explicit SixelDecoder(SixelOptions options = {});
    [[nodiscard]] bool append(std::string_view bytes);
    [[nodiscard]] std::expected<SixelImage, SixelError> finish();

private:
    enum class State : std::uint8_t
    {
        data,
        repeat,
        parameters
    };
    bool consume(char byte);
    bool applyParameters();
    bool draw(std::uint8_t bits, std::uint32_t count);
    bool ensureSize(std::uint32_t width, std::uint32_t height);
    bool fail(SixelError error);

    SixelOptions m_options;
    std::vector<std::uint16_t> m_pixels;
    std::array<std::uint32_t, 5> m_parameters{};
    std::uint32_t m_parameterIndex = 0;
    std::uint32_t m_x = 0, m_y = 0, m_width = 0, m_height = 0;
    std::uint32_t m_stride = 0, m_allocatedRows = 0, m_color = 0;
    std::size_t m_inputBytes = 0;
    std::uint64_t m_paintWork = 0;
    std::uint64_t m_resizeWork = 0;
    State m_state = State::data;
    char m_command = 0;
    SixelError m_error = SixelError::invalid;
    bool m_failed = false, m_finished = false, m_drew = false;
};
} // namespace ztermy::terminal
