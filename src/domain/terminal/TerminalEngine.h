#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <system_error>

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

    // Diagnostic and clipboard-oriented representation. Rendering will consume
    // a separate immutable cell snapshot rather than formatted text.
    [[nodiscard]] virtual std::expected<std::string, std::error_code> plainText() const = 0;

protected:
    TerminalEngine() = default;
};

} // namespace ztermy::terminal
