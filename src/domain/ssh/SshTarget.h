#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ssh
{

struct SshTarget final
{
    std::string username;
    std::string host;
    std::uint16_t port = 22;

    friend bool operator==(const SshTarget &, const SshTarget &) = default;
};

enum class SshTargetError : std::uint8_t
{
    InvalidFormat,
    MissingUsername,
    MissingHost,
    InvalidPort,
    BracketsRequired,
};

[[nodiscard]] std::expected<SshTarget, SshTargetError> parseSshTarget(std::string_view value);

} // namespace ztermy::ssh
