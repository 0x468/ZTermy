#include "application/ssh/SshConnectionRequest.h"

namespace ztermy::ssh
{
namespace
{

template <typename Endpoint>
[[nodiscard]] bool validEndpoint(const Endpoint &endpoint) noexcept
{
    if constexpr (requires { endpoint.connectionTimeoutSeconds; })
    {
        if (endpoint.connectionTimeoutSeconds == 0 || endpoint.connectionTimeoutSeconds > 300)
        {
            return false;
        }
    }
    if constexpr (requires { endpoint.authenticationTimeoutSeconds; })
    {
        if (endpoint.authenticationTimeoutSeconds == 0 || endpoint.authenticationTimeoutSeconds > 300)
        {
            return false;
        }
    }
    if (endpoint.host.trimmed().isEmpty() || endpoint.port == 0 || endpoint.username.isEmpty()
        || !validSshProxyOptions(endpoint.proxy)
        || (endpoint.proxy.username.empty() ? !endpoint.proxySecret.empty() : endpoint.proxySecret.empty()))
    {
        return false;
    }
    switch (endpoint.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            return !endpoint.privateKeyPath.isEmpty();
        case SshAuthenticationMethod::Password:
            return endpoint.privateKeyPath.isEmpty() && !endpoint.secret.empty();
        case SshAuthenticationMethod::Agent:
            return endpoint.privateKeyPath.isEmpty() && endpoint.secret.empty();
    }
    return false;
}

} // namespace

bool validSshConnectionRequest(const SshConnectionRequest &request) noexcept
{
    if (request.knownHostsPath.isEmpty() || !validSshSessionOptions(request.sessionOptions) || !validEndpoint(request)
        || request.jumpHosts.size() > maximumSshJumpHostCount)
    {
        return false;
    }
    for (std::size_t index = 0; index < request.jumpHosts.size(); ++index)
    {
        const SshJumpHostRequest &jump = request.jumpHosts[index];
        if (jump.profileId.isEmpty() || jump.displayName.isEmpty() || !validEndpoint(jump))
        {
            return false;
        }
        for (std::size_t following = index + 1; following < request.jumpHosts.size(); ++following)
        {
            if (request.jumpHosts[following].profileId == jump.profileId)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace ztermy::ssh
