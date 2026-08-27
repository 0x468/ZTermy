#pragma once

#include "infrastructure/ssh/SshByteTransport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <string_view>

namespace ztermy::ssh
{

class WindowsTcpListener;

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

struct TcpConnectError final
{
    TcpConnectErrorKind kind = TcpConnectErrorKind::SystemError;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const TcpConnectError &, const TcpConnectError &) = default;
};

struct TcpConnectTimings final
{
    std::chrono::milliseconds resolution{};
    std::chrono::milliseconds connection{};
    std::size_t candidatesAttempted = 0;
};

class WindowsWaitEvent final
{
public:
    WindowsWaitEvent() noexcept;
    ~WindowsWaitEvent();

    WindowsWaitEvent(const WindowsWaitEvent &) = delete;
    WindowsWaitEvent &operator=(const WindowsWaitEvent &) = delete;
    WindowsWaitEvent(WindowsWaitEvent &&) = delete;
    WindowsWaitEvent &operator=(WindowsWaitEvent &&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uintptr_t nativeHandle() const noexcept;
    [[nodiscard]] bool signal() noexcept;
    [[nodiscard]] bool reset() noexcept;

private:
    std::uintptr_t m_event = 0;
};

class WindowsTcpSocket final : public SshByteTransport
{
public:
    WindowsTcpSocket() noexcept = default;
    ~WindowsTcpSocket() override;

    WindowsTcpSocket(const WindowsTcpSocket &) = delete;
    WindowsTcpSocket &operator=(const WindowsTcpSocket &) = delete;

    WindowsTcpSocket(WindowsTcpSocket &&other) noexcept;
    WindowsTcpSocket &operator=(WindowsTcpSocket &&other) noexcept;

    [[nodiscard]] static std::expected<WindowsTcpSocket, TcpConnectError>
    connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout,
            const std::stop_token &stopToken = {}, TcpConnectTimings *timings = nullptr) noexcept;

    [[nodiscard]] bool valid() const noexcept override;
    [[nodiscard]] std::uintptr_t nativeHandle() const noexcept;
    [[nodiscard]] std::expected<std::size_t, SshByteTransportError> read(std::span<char> buffer) noexcept override;
    [[nodiscard]] std::expected<std::size_t, SshByteTransportError>
    write(std::span<const char> buffer) noexcept override;
    [[nodiscard]] std::expected<void, SshByteTransportError>
    waitUntilReady(SocketIoInterest interest, std::chrono::steady_clock::time_point deadline,
                   const std::stop_token &stopToken = {}, std::uintptr_t interruptHandle = 0) noexcept override;
    [[nodiscard]] std::expected<void, SshByteTransportError> shutdownWrite() noexcept;

    void close() noexcept;

private:
    friend class WindowsTcpListener;

    explicit WindowsTcpSocket(std::uintptr_t socket) noexcept;

    [[nodiscard]] std::uintptr_t release() noexcept;

    static constexpr std::uintptr_t InvalidSocket = ~std::uintptr_t{0};
    std::uintptr_t m_socket = InvalidSocket;
};

} // namespace ztermy::ssh
