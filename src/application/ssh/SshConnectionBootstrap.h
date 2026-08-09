#pragma once

#include "application/ssh/SshConnectionRequest.h"
#include "domain/ssh/SshConnectionState.h"
#include "infrastructure/ssh/Libssh2Session.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <stop_token>

namespace ztermy::ssh
{

enum class UnknownHostKeyDecision : std::uint8_t
{
    AcceptOnce,
    AcceptAndRemember,
    Reject,
};

struct SshConnectionCallbacks final
{
    std::function<void(SshConnectionPhase)> phaseChanged;
    std::function<UnknownHostKeyDecision(const QString &algorithm, const QString &fingerprint)> confirmUnknownHostKey;
    std::function<void(const QString &algorithm, const QString &fingerprint)> hostKeyChanged;
};

enum class SshBootstrapErrorReason : std::uint8_t
{
    ConnectionFailure,
    KnownHostsSaveFailed,
};

struct SshBootstrapError final
{
    SshFailureKind failure = SshFailureKind::ProtocolError;
    SshBootstrapErrorReason reason = SshBootstrapErrorReason::ConnectionFailure;

    bool operator==(const SshBootstrapError &) const = default;
};

struct AuthenticatedSshConnection final
{
    std::unique_ptr<SshByteTransport> transport;
    std::unique_ptr<Libssh2Session> session;
};

[[nodiscard]] SshFailureKind sshFailureFromTransport(const SshTransportError &error,
                                                     bool openingChannel = false) noexcept;

[[nodiscard]] std::expected<AuthenticatedSshConnection, SshBootstrapError>
establishAuthenticatedSshConnection(SshConnectionRequest &request, const SshConnectionCallbacks &callbacks,
                                    const std::stop_token &stopToken = {});

} // namespace ztermy::ssh
