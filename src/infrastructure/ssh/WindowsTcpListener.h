#pragma once

#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string_view>

namespace ztermy::ssh
{

enum class TcpListenErrorKind : std::uint8_t
{
    InvalidEndpoint,
    AddressInUse,
    AccessDenied,
    TimedOut,
    Cancelled,
    SystemError,
};

struct TcpListenError final
{
    TcpListenErrorKind kind = TcpListenErrorKind::SystemError;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const TcpListenError &, const TcpListenError &) = default;
};

class WindowsTcpListener final
{
public:
    WindowsTcpListener() noexcept = default;
    ~WindowsTcpListener();

    WindowsTcpListener(const WindowsTcpListener &) = delete;
    WindowsTcpListener &operator=(const WindowsTcpListener &) = delete;
    WindowsTcpListener(WindowsTcpListener &&other) noexcept;
    WindowsTcpListener &operator=(WindowsTcpListener &&other) noexcept;

    [[nodiscard]] static std::expected<WindowsTcpListener, TcpListenError>
    listen(std::string_view host, std::uint16_t port, int backlog = 32) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint16_t boundPort() const noexcept;
    [[nodiscard]] std::expected<void, TcpListenError> waitForClient(std::chrono::steady_clock::time_point deadline,
                                                                    const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<std::optional<WindowsTcpSocket>, TcpListenError> accept() noexcept;

    void close() noexcept;

private:
    WindowsTcpListener(std::uintptr_t socket, std::uint16_t boundPort) noexcept;
    [[nodiscard]] std::uintptr_t release() noexcept;

    static constexpr std::uintptr_t InvalidSocket = ~std::uintptr_t{0};
    std::uintptr_t m_socket = InvalidSocket;
    std::uint16_t m_boundPort = 0;
};

} // namespace ztermy::ssh
