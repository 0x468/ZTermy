#pragma once

#include "core/terminal/TerminalIdentity.h"
#include "domain/ssh/SshProfile.h"
#include "infrastructure/ssh/Libssh2Session.h"

#include <algorithm>
#include <span>
#include <vector>

namespace ztermy::ssh
{
// Views borrow the request's explicit values or static identity strings.
inline std::vector<SshTerminalEnvironment> terminalEnvironment(const std::span<const SshEnvironmentVariable> configured)
{
    std::vector<SshTerminalEnvironment> result;
    result.reserve(terminal::identityEnvironment.size() + configured.size());
    for (const auto &entry : terminal::identityEnvironment)
    {
        if (std::ranges::none_of(configured, [&](const auto &variable) {
                return variable.name == entry.name;
            }))
            result.push_back({.name = entry.name, .value = entry.value, .required = false});
    }
    for (const auto &entry : configured)
        result.push_back({.name = entry.name, .value = entry.value});
    return result;
}
} // namespace ztermy::ssh
