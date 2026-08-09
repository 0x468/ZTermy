#include "infrastructure/ssh/SshDirectTcpipTransport.h"

#include <chrono>
#include <new>
#include <utility>

using namespace std::chrono_literals;

namespace ztermy::ssh
{

std::expected<std::unique_ptr<SshDirectTcpipTransport>, SshTransportError>
SshDirectTcpipTransport::create(std::unique_ptr<SshByteTransport> upstreamTransport,
                                std::unique_ptr<Libssh2Session> upstreamSession, const std::string_view targetHost,
                                const std::uint16_t targetPort, const std::chrono::milliseconds timeout,
                                const std::stop_token &stopToken) noexcept
{
    if (!upstreamTransport || !upstreamSession || !upstreamTransport->valid() || !upstreamSession->authenticated())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    auto opened = upstreamSession->openDirectTcpip(*upstreamTransport, targetHost, targetPort, timeout, stopToken);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }
    try
    {
        return std::unique_ptr<SshDirectTcpipTransport>(
            new SshDirectTcpipTransport(std::move(upstreamTransport), std::move(upstreamSession)));
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
}

SshDirectTcpipTransport::SshDirectTcpipTransport(std::unique_ptr<SshByteTransport> upstreamTransport,
                                                 std::unique_ptr<Libssh2Session> upstreamSession) noexcept
    : m_upstreamTransport(std::move(upstreamTransport)), m_upstreamSession(std::move(upstreamSession))
{
}

SshDirectTcpipTransport::~SshDirectTcpipTransport()
{
    if (m_upstreamTransport && m_upstreamSession && m_upstreamSession->directTcpipOpen())
    {
        [[maybe_unused]] const auto closeResult = m_upstreamSession->closeDirectTcpip(*m_upstreamTransport, 2s);
    }
}

bool SshDirectTcpipTransport::valid() const noexcept
{
    return m_upstreamTransport && m_upstreamSession && m_upstreamTransport->valid()
           && m_upstreamSession->directTcpipOpen();
}

std::expected<std::size_t, SshByteTransportError> SshDirectTcpipTransport::read(const std::span<char> buffer) noexcept
{
    if (!valid())
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::Closed});
    }
    return m_upstreamSession->readDirectTcpip(buffer);
}

std::expected<std::size_t, SshByteTransportError>
SshDirectTcpipTransport::write(const std::span<const char> buffer) noexcept
{
    if (!valid())
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::Closed});
    }
    return m_upstreamSession->writeDirectTcpip(buffer);
}

std::expected<void, SshByteTransportError>
SshDirectTcpipTransport::waitUntilReady(const SocketIoInterest interest,
                                        const std::chrono::steady_clock::time_point deadline,
                                        const std::stop_token &stopToken, const std::uintptr_t interruptHandle) noexcept
{
    (void)interest;
    if (!valid())
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::Closed});
    }
    return m_upstreamSession->waitDirectTcpip(*m_upstreamTransport, deadline, stopToken, interruptHandle);
}

} // namespace ztermy::ssh
