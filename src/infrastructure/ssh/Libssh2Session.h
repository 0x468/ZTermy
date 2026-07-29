#pragma once

#include "infrastructure/ssh/Libssh2Runtime.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>

namespace ztermy::ssh
{

enum class SshTransportErrorKind : std::uint8_t
{
    InitializationFailed,
    InvalidState,
    TimedOut,
    Cancelled,
    ConnectionLost,
    ProtocolError,
};

struct SshTransportError final
{
    SshTransportErrorKind kind = SshTransportErrorKind::ProtocolError;
    int libssh2Code = 0;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const SshTransportError &, const SshTransportError &) = default;
};

class Libssh2Session final
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Libssh2Session>, SshTransportError> create() noexcept;

    ~Libssh2Session();

    Libssh2Session(const Libssh2Session &) = delete;
    Libssh2Session &operator=(const Libssh2Session &) = delete;
    Libssh2Session(Libssh2Session &&) = delete;
    Libssh2Session &operator=(Libssh2Session &&) = delete;

    [[nodiscard]] std::expected<void, SshTransportError> handshake(WindowsTcpSocket &socket,
                                                                   std::chrono::milliseconds timeout,
                                                                   const std::stop_token &stopToken = {}) noexcept;

    [[nodiscard]] bool handshakeComplete() const noexcept;

private:
    Libssh2Session(std::unique_ptr<Libssh2Runtime> runtime, void *session) noexcept;

    std::unique_ptr<Libssh2Runtime> m_runtime;
    void *m_session = nullptr;
    bool m_handshakeComplete = false;
};

} // namespace ztermy::ssh
