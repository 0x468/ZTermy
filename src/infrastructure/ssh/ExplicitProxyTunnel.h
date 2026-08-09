#pragma once

#include "domain/ssh/SshProfile.h"
#include "infrastructure/ssh/SshByteTransport.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <string_view>

namespace ztermy::ssh
{

enum class ExplicitProxyErrorKind : std::uint8_t
{
    InvalidConfiguration,
    AuthenticationRequired,
    AuthenticationRejected,
    ConnectionRejected,
    ProtocolError,
    TimedOut,
    Cancelled,
    ConnectionLost,
};

struct ExplicitProxyError final
{
    ExplicitProxyErrorKind kind = ExplicitProxyErrorKind::ProtocolError;
    int protocolCode = 0;

    [[nodiscard]] friend bool operator==(const ExplicitProxyError &, const ExplicitProxyError &) = default;
};

[[nodiscard]] std::expected<void, ExplicitProxyError>
establishExplicitProxyTunnel(SshByteTransport &transport, SshProxyType type, std::string_view targetHost,
                             std::uint16_t targetPort, std::string_view username, std::string_view password,
                             std::chrono::milliseconds timeout, const std::stop_token &stopToken = {});

} // namespace ztermy::ssh
