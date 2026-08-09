#include "application/ssh/SshConnectionRequest.h"

namespace ztermy::ssh
{

bool validSshConnectionRequest(const SshConnectionRequest &request) noexcept
{
    if (request.host.trimmed().isEmpty() || request.port == 0 || request.username.isEmpty()
        || request.knownHostsPath.isEmpty() || !validSshSessionOptions(request.sessionOptions))
    {
        return false;
    }
    switch (request.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            return !request.privateKeyPath.isEmpty();
        case SshAuthenticationMethod::Password:
            return request.privateKeyPath.isEmpty() && !request.secret.empty();
        case SshAuthenticationMethod::Agent:
            return request.privateKeyPath.isEmpty() && request.secret.empty();
    }
    return false;
}

} // namespace ztermy::ssh
