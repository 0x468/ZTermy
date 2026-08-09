#pragma once

#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/SshByteTransport.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <string_view>

namespace ztermy::ssh
{

class SshDirectTcpipTransport final : public SshByteTransport
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<SshDirectTcpipTransport>, SshTransportError>
    create(std::unique_ptr<SshByteTransport> upstreamTransport, std::unique_ptr<Libssh2Session> upstreamSession,
           std::string_view targetHost, std::uint16_t targetPort, std::chrono::milliseconds timeout,
           const std::stop_token &stopToken = {}) noexcept;

    ~SshDirectTcpipTransport() override;

    SshDirectTcpipTransport(const SshDirectTcpipTransport &) = delete;
    SshDirectTcpipTransport &operator=(const SshDirectTcpipTransport &) = delete;
    SshDirectTcpipTransport(SshDirectTcpipTransport &&) = delete;
    SshDirectTcpipTransport &operator=(SshDirectTcpipTransport &&) = delete;

    [[nodiscard]] bool valid() const noexcept override;
    [[nodiscard]] std::expected<std::size_t, SshByteTransportError> read(std::span<char> buffer) noexcept override;
    [[nodiscard]] std::expected<std::size_t, SshByteTransportError>
    write(std::span<const char> buffer) noexcept override;
    [[nodiscard]] std::expected<void, SshByteTransportError>
    waitUntilReady(SocketIoInterest interest, std::chrono::steady_clock::time_point deadline,
                   const std::stop_token &stopToken = {}, std::uintptr_t interruptHandle = 0) noexcept override;

private:
    SshDirectTcpipTransport(std::unique_ptr<SshByteTransport> upstreamTransport,
                            std::unique_ptr<Libssh2Session> upstreamSession) noexcept;

    std::unique_ptr<SshByteTransport> m_upstreamTransport;
    std::unique_ptr<Libssh2Session> m_upstreamSession;
};

} // namespace ztermy::ssh
