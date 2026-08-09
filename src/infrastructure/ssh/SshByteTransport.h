#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>

namespace ztermy::ssh
{

enum class SocketIoInterest : std::uint8_t
{
    Read,
    Write,
    ReadWrite,
};

enum class SshByteTransportErrorKind : std::uint8_t
{
    WouldBlock,
    Closed,
    TimedOut,
    Cancelled,
    SystemError,
};

struct SshByteTransportError final
{
    SshByteTransportErrorKind kind = SshByteTransportErrorKind::SystemError;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const SshByteTransportError &, const SshByteTransportError &) = default;
};

class SshByteTransport
{
public:
    virtual ~SshByteTransport() = default;

    SshByteTransport(const SshByteTransport &) = delete;
    SshByteTransport &operator=(const SshByteTransport &) = delete;
    SshByteTransport(SshByteTransport &&) = delete;
    SshByteTransport &operator=(SshByteTransport &&) = delete;

    [[nodiscard]] virtual bool valid() const noexcept = 0;
    [[nodiscard]] virtual std::expected<std::size_t, SshByteTransportError> read(std::span<char> buffer) noexcept = 0;
    [[nodiscard]] virtual std::expected<std::size_t, SshByteTransportError>
    write(std::span<const char> buffer) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, SshByteTransportError>
    waitUntilReady(SocketIoInterest interest, std::chrono::steady_clock::time_point deadline,
                   const std::stop_token &stopToken = {}, std::uintptr_t interruptHandle = 0) noexcept = 0;

protected:
    SshByteTransport() noexcept = default;
};

} // namespace ztermy::ssh
