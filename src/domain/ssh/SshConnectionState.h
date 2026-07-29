#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace ztermy::ssh
{

enum class SshConnectionPhase : std::uint8_t
{
    Disconnected,
    Resolving,
    Connecting,
    Handshaking,
    VerifyingHostKey,
    AwaitingHostKeyConfirmation,
    Authenticating,
    OpeningChannel,
    Connected,
    Closing,
    Failed,
};

enum class SshFailureKind : std::uint8_t
{
    NameResolutionFailed,
    ConnectionRefused,
    TimedOut,
    TransportError,
    HostKeyChanged,
    HostKeyInvalid,
    AuthenticationRejected,
    AuthenticationUnavailable,
    ChannelOpenFailed,
    RemoteClosed,
    Cancelled,
    ProtocolError,
};

enum class SshStateError : std::uint8_t
{
    InvalidTransition,
    InvalidFailure,
};

class SshConnectionState final
{
public:
    [[nodiscard]] SshConnectionPhase phase() const noexcept;
    [[nodiscard]] std::optional<SshFailureKind> failure() const noexcept;

    [[nodiscard]] std::expected<void, SshStateError> start() noexcept;
    [[nodiscard]] std::expected<void, SshStateError> advanceTo(SshConnectionPhase next) noexcept;
    [[nodiscard]] std::expected<void, SshStateError> requestClose() noexcept;
    [[nodiscard]] std::expected<void, SshStateError> completeClose() noexcept;
    [[nodiscard]] std::expected<void, SshStateError> fail(SshFailureKind failure) noexcept;
    [[nodiscard]] std::expected<void, SshStateError> resetFailure() noexcept;

private:
    SshConnectionPhase m_phase = SshConnectionPhase::Disconnected;
    std::optional<SshFailureKind> m_failure;
};

} // namespace ztermy::ssh
