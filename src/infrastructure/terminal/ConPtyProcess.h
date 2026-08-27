#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace ztermy::terminal
{

struct TerminalSize
{
    std::uint16_t columns = 80;
    std::uint16_t rows = 24;

    [[nodiscard]] bool valid() const noexcept
    {
        constexpr auto maximumCoordinate = static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max());
        return columns > 0 && columns <= maximumCoordinate && rows > 0 && rows <= maximumCoordinate;
    }
};

class ConPtyProcess final
{
public:
    ConPtyProcess();
    ~ConPtyProcess();

    ConPtyProcess(const ConPtyProcess &) = delete;
    ConPtyProcess &operator=(const ConPtyProcess &) = delete;
    ConPtyProcess(ConPtyProcess &&) = delete;
    ConPtyProcess &operator=(ConPtyProcess &&) = delete;

    [[nodiscard]] std::error_code start(std::wstring applicationName, std::wstring commandLine, TerminalSize size,
                                        std::wstring_view workingDirectory = {});
    [[nodiscard]] std::expected<std::size_t, std::error_code> read(std::span<std::byte> destination);
    [[nodiscard]] std::error_code write(std::span<const std::byte> source);
    [[nodiscard]] std::error_code resize(TerminalSize size);
    [[nodiscard]] std::expected<bool, std::error_code> waitForExit(std::chrono::milliseconds timeout) const;

    [[nodiscard]] bool running() const noexcept;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ztermy::terminal
