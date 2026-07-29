#include "domain/ssh/SshConnectionState.h"

namespace
{

[[nodiscard]] bool isForwardTransition(const ztermy::ssh::SshConnectionPhase current,
                                       const ztermy::ssh::SshConnectionPhase next) noexcept
{
    using enum ztermy::ssh::SshConnectionPhase;

    switch (current)
    {
        case Resolving:
            return next == Connecting;
        case Connecting:
            return next == Handshaking;
        case Handshaking:
            return next == VerifyingHostKey;
        case VerifyingHostKey:
            return next == AwaitingHostKeyConfirmation || next == Authenticating;
        case AwaitingHostKeyConfirmation:
            return next == Authenticating;
        case Authenticating:
            return next == OpeningChannel;
        case OpeningChannel:
            return next == Connected;
        case Disconnected:
        case Connected:
        case Closing:
        case Failed:
            return false;
    }

    return false;
}

} // namespace

namespace ztermy::ssh
{

SshConnectionPhase SshConnectionState::phase() const noexcept
{
    return m_phase;
}

std::optional<SshFailureKind> SshConnectionState::failure() const noexcept
{
    return m_failure;
}

std::expected<void, SshStateError> SshConnectionState::start() noexcept
{
    if (m_phase != SshConnectionPhase::Disconnected)
    {
        return std::unexpected(SshStateError::InvalidTransition);
    }

    m_phase = SshConnectionPhase::Resolving;
    m_failure.reset();
    return {};
}

std::expected<void, SshStateError> SshConnectionState::advanceTo(const SshConnectionPhase next) noexcept
{
    if (!isForwardTransition(m_phase, next))
    {
        return std::unexpected(SshStateError::InvalidTransition);
    }

    m_phase = next;
    return {};
}

std::expected<void, SshStateError> SshConnectionState::requestClose() noexcept
{
    if (m_phase == SshConnectionPhase::Disconnected || m_phase == SshConnectionPhase::Closing
        || m_phase == SshConnectionPhase::Failed)
    {
        return std::unexpected(SshStateError::InvalidTransition);
    }

    m_phase = SshConnectionPhase::Closing;
    return {};
}

std::expected<void, SshStateError> SshConnectionState::completeClose() noexcept
{
    if (m_phase != SshConnectionPhase::Closing)
    {
        return std::unexpected(SshStateError::InvalidTransition);
    }

    m_phase = SshConnectionPhase::Disconnected;
    m_failure.reset();
    return {};
}

std::expected<void, SshStateError> SshConnectionState::fail(const SshFailureKind failure) noexcept
{
    if (m_phase == SshConnectionPhase::Disconnected || m_phase == SshConnectionPhase::Closing
        || m_phase == SshConnectionPhase::Failed)
    {
        return std::unexpected(SshStateError::InvalidFailure);
    }

    m_phase = SshConnectionPhase::Failed;
    m_failure = failure;
    return {};
}

std::expected<void, SshStateError> SshConnectionState::resetFailure() noexcept
{
    if (m_phase != SshConnectionPhase::Failed)
    {
        return std::unexpected(SshStateError::InvalidTransition);
    }

    m_phase = SshConnectionPhase::Disconnected;
    m_failure.reset();
    return {};
}

} // namespace ztermy::ssh
