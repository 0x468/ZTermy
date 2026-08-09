#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ztermy::forwarding
{

inline constexpr std::size_t maximumSocks5HandshakeBytes = 512;

enum class Socks5HandshakeStatus : std::uint8_t
{
    NeedMore,
    SendMethodSelection,
    DestinationReady,
    Rejected,
};

enum class Socks5ReplyCode : std::uint8_t
{
    Succeeded = 0x00,
    GeneralFailure = 0x01,
    ConnectionNotAllowed = 0x02,
    NetworkUnreachable = 0x03,
    HostUnreachable = 0x04,
    ConnectionRefused = 0x05,
    TimedOut = 0x06,
    CommandNotSupported = 0x07,
    AddressTypeNotSupported = 0x08,
};

enum class Socks5ProtocolError : std::uint8_t
{
    InvalidState,
    ResourceLimit,
};

struct Socks5Destination final
{
    std::string host;
    std::uint16_t port = 0;

    friend bool operator==(const Socks5Destination &, const Socks5Destination &) = default;
};

struct Socks5HandshakeResult final
{
    Socks5HandshakeStatus status = Socks5HandshakeStatus::NeedMore;
    std::vector<std::byte> response;
    std::optional<Socks5Destination> destination;
    std::vector<std::byte> remainingData;
};

class Socks5Handshake final
{
public:
    [[nodiscard]] std::expected<Socks5HandshakeResult, Socks5ProtocolError>
    consume(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] static std::vector<std::byte> reply(Socks5ReplyCode code);

private:
    enum class State : std::uint8_t
    {
        Greeting,
        Request,
        Complete,
        Failed,
    };

    [[nodiscard]] std::expected<Socks5HandshakeResult, Socks5ProtocolError> consumeGreeting() noexcept;
    [[nodiscard]] std::expected<Socks5HandshakeResult, Socks5ProtocolError> consumeRequest() noexcept;

    State m_state = State::Greeting;
    std::vector<std::byte> m_buffer;
};

} // namespace ztermy::forwarding
