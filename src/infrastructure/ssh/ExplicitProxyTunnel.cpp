#include "infrastructure/ssh/ExplicitProxyTunnel.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using ztermy::ssh::ExplicitProxyError;
using ztermy::ssh::ExplicitProxyErrorKind;
using ztermy::ssh::SocketIoInterest;
using ztermy::ssh::SshByteTransport;
using ztermy::ssh::SshByteTransportError;
using ztermy::ssh::SshByteTransportErrorKind;

[[nodiscard]] ExplicitProxyError mapTransportError(const SshByteTransportError &error) noexcept
{
    switch (error.kind)
    {
        case SshByteTransportErrorKind::TimedOut:
            return {.kind = ExplicitProxyErrorKind::TimedOut, .protocolCode = error.nativeCode};
        case SshByteTransportErrorKind::Cancelled:
            return {.kind = ExplicitProxyErrorKind::Cancelled, .protocolCode = error.nativeCode};
        case SshByteTransportErrorKind::WouldBlock:
        case SshByteTransportErrorKind::Closed:
        case SshByteTransportErrorKind::SystemError:
            return {.kind = ExplicitProxyErrorKind::ConnectionLost, .protocolCode = error.nativeCode};
    }
    return {.kind = ExplicitProxyErrorKind::ConnectionLost, .protocolCode = error.nativeCode};
}

[[nodiscard]] std::expected<void, ExplicitProxyError> waitFor(SshByteTransport &transport,
                                                              const SocketIoInterest interest,
                                                              const std::chrono::steady_clock::time_point deadline,
                                                              const std::stop_token &stopToken) noexcept
{
    auto ready = transport.waitUntilReady(interest, deadline, stopToken);
    if (!ready)
    {
        return std::unexpected(mapTransportError(ready.error()));
    }
    return {};
}

[[nodiscard]] std::expected<void, ExplicitProxyError> writeAll(SshByteTransport &transport,
                                                               const std::span<const char> bytes,
                                                               const std::chrono::steady_clock::time_point deadline,
                                                               const std::stop_token &stopToken) noexcept
{
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::Cancelled});
        }
        auto written = transport.write(bytes.subspan(offset));
        if (written && *written != 0)
        {
            offset += *written;
            continue;
        }
        if (written || written.error().kind == SshByteTransportErrorKind::WouldBlock)
        {
            auto ready = waitFor(transport, SocketIoInterest::Write, deadline, stopToken);
            if (!ready)
            {
                return ready;
            }
            continue;
        }
        return std::unexpected(mapTransportError(written.error()));
    }
    return {};
}

[[nodiscard]] std::expected<void, ExplicitProxyError> readExact(SshByteTransport &transport,
                                                                const std::span<char> bytes,
                                                                const std::chrono::steady_clock::time_point deadline,
                                                                const std::stop_token &stopToken) noexcept
{
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::Cancelled});
        }
        auto received = transport.read(bytes.subspan(offset));
        if (received && *received != 0)
        {
            offset += *received;
            continue;
        }
        if (received || received.error().kind == SshByteTransportErrorKind::WouldBlock)
        {
            auto ready = waitFor(transport, SocketIoInterest::Read, deadline, stopToken);
            if (!ready)
            {
                return ready;
            }
            continue;
        }
        return std::unexpected(mapTransportError(received.error()));
    }
    return {};
}

[[nodiscard]] std::expected<void, ExplicitProxyError>
establishSocks5(SshByteTransport &transport, const std::string_view targetHost, const std::uint16_t targetPort,
                const std::string_view username, const std::string_view password,
                const std::chrono::steady_clock::time_point deadline, const std::stop_token &stopToken)
{
    if (targetHost.empty() || targetHost.size() > std::numeric_limits<std::uint8_t>::max()
        || username.size() > std::numeric_limits<std::uint8_t>::max()
        || password.size() > std::numeric_limits<std::uint8_t>::max() || (username.empty() != password.empty()))
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::InvalidConfiguration});
    }

    const bool authenticate = !username.empty();
    const std::array<char, 3> greeting{char{5}, char{1}, authenticate ? char{2} : char{0}};
    auto operation = writeAll(transport, greeting, deadline, stopToken);
    if (!operation)
    {
        return operation;
    }

    std::array<char, 2> methodResponse{};
    operation = readExact(transport, methodResponse, deadline, stopToken);
    if (!operation)
    {
        return operation;
    }
    if (static_cast<unsigned char>(methodResponse[0]) != 5)
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }
    const auto selectedMethod = static_cast<unsigned char>(methodResponse[1]);
    if (selectedMethod == 0xFF)
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::AuthenticationRequired});
    }
    if (selectedMethod != (authenticate ? 2U : 0U))
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }

    if (authenticate)
    {
        std::vector<char> authenticationRequest;
        authenticationRequest.reserve(3 + username.size() + password.size());
        authenticationRequest.push_back(char{1});
        authenticationRequest.push_back(static_cast<char>(username.size()));
        authenticationRequest.insert(authenticationRequest.end(), username.begin(), username.end());
        authenticationRequest.push_back(static_cast<char>(password.size()));
        authenticationRequest.insert(authenticationRequest.end(), password.begin(), password.end());
        operation = writeAll(transport, authenticationRequest, deadline, stopToken);
        if (!operation)
        {
            return operation;
        }

        std::array<char, 2> authenticationResponse{};
        operation = readExact(transport, authenticationResponse, deadline, stopToken);
        if (!operation)
        {
            return operation;
        }
        if (static_cast<unsigned char>(authenticationResponse[0]) != 1
            || static_cast<unsigned char>(authenticationResponse[1]) != 0)
        {
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::AuthenticationRejected});
        }
    }

    std::vector<char> connectRequest;
    connectRequest.reserve(7 + targetHost.size());
    connectRequest.insert(connectRequest.end(),
                          {char{5}, char{1}, char{0}, char{3}, static_cast<char>(targetHost.size())});
    connectRequest.insert(connectRequest.end(), targetHost.begin(), targetHost.end());
    connectRequest.push_back(static_cast<char>((targetPort >> 8U) & 0xFFU));
    connectRequest.push_back(static_cast<char>(targetPort & 0xFFU));
    operation = writeAll(transport, connectRequest, deadline, stopToken);
    if (!operation)
    {
        return operation;
    }

    std::array<char, 4> responseHeader{};
    operation = readExact(transport, responseHeader, deadline, stopToken);
    if (!operation)
    {
        return operation;
    }
    const auto responseCode = static_cast<unsigned char>(responseHeader[1]);
    if (static_cast<unsigned char>(responseHeader[0]) != 5 || static_cast<unsigned char>(responseHeader[2]) != 0)
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }
    if (responseCode != 0)
    {
        return std::unexpected(
            ExplicitProxyError{.kind = ExplicitProxyErrorKind::ConnectionRejected, .protocolCode = responseCode});
    }

    std::size_t addressLength = 0;
    switch (static_cast<unsigned char>(responseHeader[3]))
    {
        case 1:
            addressLength = 4;
            break;
        case 4:
            addressLength = 16;
            break;
        case 3:
        {
            std::array<char, 1> length{};
            operation = readExact(transport, length, deadline, stopToken);
            if (!operation)
            {
                return operation;
            }
            addressLength = static_cast<unsigned char>(length[0]);
            break;
        }
        default:
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }
    std::array<char, 258> ignoredAddressAndPort{};
    return readExact(transport, std::span(ignoredAddressAndPort).first(addressLength + 2), deadline, stopToken);
}

[[nodiscard]] std::string base64Encode(const std::string_view value)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((value.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < value.size(); index += 3)
    {
        const std::uint32_t first = static_cast<unsigned char>(value[index]);
        const std::uint32_t second = index + 1 < value.size() ? static_cast<unsigned char>(value[index + 1]) : 0;
        const std::uint32_t third = index + 2 < value.size() ? static_cast<unsigned char>(value[index + 2]) : 0;
        const std::uint32_t combined = (first << 16U) | (second << 8U) | third;
        encoded.push_back(alphabet[(combined >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(combined >> 12U) & 0x3FU]);
        encoded.push_back(index + 1 < value.size() ? alphabet[(combined >> 6U) & 0x3FU] : '=');
        encoded.push_back(index + 2 < value.size() ? alphabet[combined & 0x3FU] : '=');
    }
    return encoded;
}

[[nodiscard]] std::expected<void, ExplicitProxyError>
establishHttpConnect(SshByteTransport &transport, const std::string_view targetHost, const std::uint16_t targetPort,
                     const std::string_view username, const std::string_view password,
                     const std::chrono::steady_clock::time_point deadline, const std::stop_token &stopToken)
{
    if (targetHost.empty() || targetHost.size() > 1024 || targetHost.find_first_of("\r\n") != std::string_view::npos
        || username.find_first_of(":\r\n") != std::string_view::npos
        || password.find_first_of("\r\n") != std::string_view::npos || (username.empty() != password.empty()))
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::InvalidConfiguration});
    }

    const bool isIpv6Literal = targetHost.find(':') != std::string_view::npos && targetHost.front() != '[';
    std::string authority;
    authority.reserve(targetHost.size() + 8);
    if (isIpv6Literal)
    {
        authority.push_back('[');
    }
    authority.append(targetHost);
    if (isIpv6Literal)
    {
        authority.push_back(']');
    }
    authority.push_back(':');
    std::array<char, 5> portText{};
    const auto conversion = std::to_chars(portText.data(), portText.data() + portText.size(), targetPort);
    authority.append(portText.data(), conversion.ptr);

    std::string request =
        "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\nProxy-Connection: Keep-Alive\r\n";
    if (!username.empty())
    {
        std::string credentials;
        credentials.reserve(username.size() + password.size() + 1);
        credentials.append(username);
        credentials.push_back(':');
        credentials.append(password);
        request += "Proxy-Authorization: Basic " + base64Encode(credentials) + "\r\n";
        std::ranges::fill(credentials, '\0');
    }
    request += "\r\n";
    auto operation = writeAll(transport, std::span(request), deadline, stopToken);
    std::ranges::fill(request, '\0');
    if (!operation)
    {
        return operation;
    }

    constexpr std::size_t maximumHeaderBytes = std::size_t{16} * 1024U;
    std::string response;
    response.reserve(512);
    while (!response.ends_with("\r\n\r\n"))
    {
        if (response.size() >= maximumHeaderBytes)
        {
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
        }
        std::array<char, 1> byte{};
        operation = readExact(transport, byte, deadline, stopToken);
        if (!operation)
        {
            return operation;
        }
        response.push_back(byte[0]);
    }

    const std::size_t lineEnd = response.find("\r\n");
    const std::string_view statusLine(response.data(), lineEnd);
    if (!(statusLine.starts_with("HTTP/1.0 ") || statusLine.starts_with("HTTP/1.1 ")) || statusLine.size() < 12)
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }
    int statusCode = 0;
    const char *codeBegin = statusLine.data() + 9;
    const char *codeEnd = codeBegin + 3;
    const auto parsed = std::from_chars(codeBegin, codeEnd, statusCode);
    if (parsed.ec != std::errc{} || parsed.ptr != codeEnd || (statusLine.size() > 12 && statusLine[12] != ' '))
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::ProtocolError});
    }
    if (statusCode >= 200 && statusCode < 300)
    {
        return {};
    }
    return std::unexpected(ExplicitProxyError{
        .kind = statusCode == 407 ? ExplicitProxyErrorKind::AuthenticationRejected
                                  : ExplicitProxyErrorKind::ConnectionRejected,
        .protocolCode = statusCode,
    });
}

} // namespace

namespace ztermy::ssh
{

std::expected<void, ExplicitProxyError>
establishExplicitProxyTunnel(SshByteTransport &transport, const SshProxyType type, const std::string_view targetHost,
                             const std::uint16_t targetPort, const std::string_view username,
                             const std::string_view password, const std::chrono::milliseconds timeout,
                             const std::stop_token &stopToken)
{
    if (!transport.valid() || targetPort == 0 || timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::InvalidConfiguration});
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    switch (type)
    {
        case SshProxyType::Socks5:
            return establishSocks5(transport, targetHost, targetPort, username, password, deadline, stopToken);
        case SshProxyType::HttpConnect:
            return establishHttpConnect(transport, targetHost, targetPort, username, password, deadline, stopToken);
        case SshProxyType::None:
            return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::InvalidConfiguration});
    }
    return std::unexpected(ExplicitProxyError{.kind = ExplicitProxyErrorKind::InvalidConfiguration});
}

} // namespace ztermy::ssh
