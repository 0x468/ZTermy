#include "domain/ssh/SshTarget.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace
{

constexpr std::size_t maximumUsernameLength = 256;
constexpr std::size_t maximumHostLength = 1024;

[[nodiscard]] std::string_view trimAscii(std::string_view value)
{
    constexpr std::string_view whitespace{" \t\r\n"};
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1U);
}

[[nodiscard]] bool containsWhitespaceOrControl(const std::string_view value)
{
    return std::ranges::any_of(value, [](const unsigned char character) {
        return character <= 0x20U || character == 0x7FU;
    });
}

[[nodiscard]] std::expected<std::uint16_t, ztermy::ssh::SshTargetError> parsePort(const std::string_view value)
{
    if (value.empty())
    {
        return std::unexpected(ztermy::ssh::SshTargetError::InvalidPort);
    }
    unsigned int port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0
        || port > std::numeric_limits<std::uint16_t>::max())
    {
        return std::unexpected(ztermy::ssh::SshTargetError::InvalidPort);
    }
    return static_cast<std::uint16_t>(port);
}

} // namespace

namespace ztermy::ssh
{

std::expected<SshTarget, SshTargetError> parseSshTarget(const std::string_view input)
{
    const std::string_view value = trimAscii(input);
    if (value.empty() || containsWhitespaceOrControl(value))
    {
        return std::unexpected(SshTargetError::InvalidFormat);
    }

    const std::size_t separator = value.find('@');
    if (separator == std::string_view::npos || value.find('@', separator + 1U) != std::string_view::npos)
    {
        return std::unexpected(SshTargetError::InvalidFormat);
    }
    const std::string_view username = value.substr(0, separator);
    const std::string_view endpoint = value.substr(separator + 1U);
    if (username.empty())
    {
        return std::unexpected(SshTargetError::MissingUsername);
    }
    if (username.size() > maximumUsernameLength)
    {
        return std::unexpected(SshTargetError::InvalidFormat);
    }
    if (endpoint.empty())
    {
        return std::unexpected(SshTargetError::MissingHost);
    }

    std::string_view host;
    std::uint16_t port = 22;
    if (endpoint.front() == '[')
    {
        const std::size_t close = endpoint.find(']');
        if (close == std::string_view::npos)
        {
            return std::unexpected(SshTargetError::InvalidFormat);
        }
        host = endpoint.substr(1U, close - 1U);
        const std::string_view suffix = endpoint.substr(close + 1U);
        if (!suffix.empty())
        {
            if (suffix.front() != ':')
            {
                return std::unexpected(SshTargetError::InvalidFormat);
            }
            const auto parsedPort = parsePort(suffix.substr(1U));
            if (!parsedPort)
            {
                return std::unexpected(parsedPort.error());
            }
            port = *parsedPort;
        }
    }
    else
    {
        const std::size_t colon = endpoint.find(':');
        if (colon != std::string_view::npos)
        {
            if (endpoint.find(':', colon + 1U) != std::string_view::npos)
            {
                return std::unexpected(SshTargetError::BracketsRequired);
            }
            host = endpoint.substr(0, colon);
            const auto parsedPort = parsePort(endpoint.substr(colon + 1U));
            if (!parsedPort)
            {
                return std::unexpected(parsedPort.error());
            }
            port = *parsedPort;
        }
        else
        {
            host = endpoint;
        }
    }

    if (host.empty())
    {
        return std::unexpected(SshTargetError::MissingHost);
    }
    if (host.size() > maximumHostLength)
    {
        return std::unexpected(SshTargetError::InvalidFormat);
    }
    return SshTarget{.username = std::string{username}, .host = std::string{host}, .port = port};
}

} // namespace ztermy::ssh
