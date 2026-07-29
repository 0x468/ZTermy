#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <string_view>

namespace ztermy::ssh
{

enum class TcpConnectErrorKind : std::uint8_t
{
    InvalidEndpoint,
    NameResolutionFailed,
    ConnectionRefused,
    TimedOut,
    Cancelled,
    NetworkUnreachable,
    SystemError,
};

enum class SocketIoInterest : std::uint8_t
{
    Read,
    Write,
    ReadWrite,
};

struct TcpConnectError final
{
    TcpConnectErrorKind kind = TcpConnectErrorKind::SystemError;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const TcpConnectError &, const TcpConnectError &) = default;
};

class WindowsTcpSocket final
{
public:
    WindowsTcpSocket() noexcept = default;
    ~WindowsTcpSocket();

    WindowsTcpSocket(const WindowsTcpSocket &) = delete;
    WindowsTcpSocket &operator=(const WindowsTcpSocket &) = delete;

    WindowsTcpSocket(WindowsTcpSocket &&other) noexcept;
    WindowsTcpSocket &operator=(WindowsTcpSocket &&other) noexcept;

    [[nodiscard]] static std::expected<WindowsTcpSocket, TcpConnectError>
    connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout,
            const std::stop_token &stopToken = {}) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uintptr_t nativeHandle() const noexcept;
    [[nodiscard]] std::expected<void, TcpConnectError>
    waitUntilReady(SocketIoInterest interest, std::chrono::steady_clock::time_point deadline,
                   const std::stop_token &stopToken = {}) const noexcept;

    void close() noexcept;

private:
    explicit WindowsTcpSocket(std::uintptr_t socket) noexcept;

    [[nodiscard]] std::uintptr_t release() noexcept;

    static constexpr std::uintptr_t InvalidSocket = ~std::uintptr_t{0};
    std::uintptr_t m_socket = InvalidSocket;
};

} // namespace ztermy::ssh
