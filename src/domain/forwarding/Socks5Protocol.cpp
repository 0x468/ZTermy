#include "domain/forwarding/Socks5Protocol.h"

#include <algorithm>
#include <array>
#include <new>
#include <ranges>
#include <string_view>

namespace ztermy::forwarding
{
namespace
{

[[nodiscard]] std::uint8_t byteValue(const std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const std::uint8_t value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

[[nodiscard]] std::string ipv4Address(const std::span<const std::byte, 4> address)
{
    std::string result;
    result.reserve(15);
    for (std::size_t index = 0; index < address.size(); ++index)
    {
        if (index != 0)
        {
            result.push_back('.');
        }
        result += std::to_string(byteValue(address[index]));
    }
    return result;
}

[[nodiscard]] std::string ipv6Address(const std::span<const std::byte, 16> address)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(39);
    for (std::size_t group = 0; group < 8; ++group)
    {
        if (group != 0)
        {
            result.push_back(':');
        }
        const std::uint16_t value =
            static_cast<std::uint16_t>((byteValue(address[group * 2]) << 8U) | byteValue(address[group * 2 + 1]));
        bool emitted = false;
        for (int shift = 12; shift >= 0; shift -= 4)
        {
            const char digit = digits[(value >> shift) & 0x0FU];
            if (digit != '0' || emitted || shift == 0)
            {
                result.push_back(digit);
                emitted = true;
            }
        }
    }
    return result;
}

[[nodiscard]] bool validDomain(const std::string_view domain) noexcept
{
    return !domain.empty() && domain.size() <= 253 && std::ranges::all_of(domain, [](const unsigned char character) {
        return character > 0x20U && character < 0x7FU;
    });
}

} // namespace

std::expected<Socks5HandshakeResult, Socks5ProtocolError>
Socks5Handshake::consume(const std::span<const std::byte> input) noexcept
{
    if (m_state == State::Complete || m_state == State::Failed)
    {
        return std::unexpected(Socks5ProtocolError::InvalidState);
    }
    if (input.size() > maximumSocks5HandshakeBytes - m_buffer.size())
    {
        m_state = State::Failed;
        m_buffer.clear();
        return std::unexpected(Socks5ProtocolError::ResourceLimit);
    }
    try
    {
        m_buffer.insert(m_buffer.end(), input.begin(), input.end());
        return m_state == State::Greeting ? consumeGreeting() : consumeRequest();
    }
    catch (const std::bad_alloc &)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return std::unexpected(Socks5ProtocolError::ResourceLimit);
    }
}

std::expected<Socks5HandshakeResult, Socks5ProtocolError> Socks5Handshake::consumeGreeting() noexcept
{
    if (m_buffer.size() < 2)
    {
        return Socks5HandshakeResult{};
    }
    const std::size_t methodCount = byteValue(m_buffer[1]);
    if (byteValue(m_buffer[0]) != 0x05U || methodCount == 0)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected, .response = bytes({0x05U, 0xFFU})};
    }
    const std::size_t frameSize = 2 + methodCount;
    if (m_buffer.size() < frameSize)
    {
        return Socks5HandshakeResult{};
    }
    const auto methods = std::span(m_buffer).subspan(2, methodCount);
    const bool supportsNoAuthentication = std::ranges::find(methods, std::byte{0x00}) != methods.end();
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
    if (!supportsNoAuthentication)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected, .response = bytes({0x05U, 0xFFU})};
    }
    m_state = State::Request;
    return Socks5HandshakeResult{.status = Socks5HandshakeStatus::SendMethodSelection,
                                 .response = bytes({0x05U, 0x00U})};
}

std::expected<Socks5HandshakeResult, Socks5ProtocolError> Socks5Handshake::consumeRequest() noexcept
{
    if (m_buffer.size() < 4)
    {
        return Socks5HandshakeResult{};
    }
    if (byteValue(m_buffer[0]) != 0x05U || byteValue(m_buffer[2]) != 0x00U)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                     .response = reply(Socks5ReplyCode::GeneralFailure)};
    }
    if (byteValue(m_buffer[1]) != 0x01U)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                     .response = reply(Socks5ReplyCode::CommandNotSupported)};
    }

    std::size_t addressOffset = 4;
    std::size_t addressSize = 0;
    const std::uint8_t addressType = byteValue(m_buffer[3]);
    if (addressType == 0x01U)
    {
        addressSize = 4;
    }
    else if (addressType == 0x04U)
    {
        addressSize = 16;
    }
    else if (addressType == 0x03U)
    {
        if (m_buffer.size() < 5)
        {
            return Socks5HandshakeResult{};
        }
        addressSize = byteValue(m_buffer[4]);
        addressOffset = 5;
        if (addressSize == 0 || addressSize > 253)
        {
            m_state = State::Failed;
            m_buffer.clear();
            return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                         .response = reply(Socks5ReplyCode::AddressTypeNotSupported)};
        }
    }
    else
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                     .response = reply(Socks5ReplyCode::AddressTypeNotSupported)};
    }

    const std::size_t frameSize = addressOffset + addressSize + 2;
    if (m_buffer.size() < frameSize)
    {
        return Socks5HandshakeResult{};
    }
    std::string host;
    if (addressType == 0x01U)
    {
        const auto address = std::span(m_buffer).subspan(addressOffset, addressSize);
        host = ipv4Address(std::span<const std::byte, 4>(address.data(), 4));
    }
    else if (addressType == 0x04U)
    {
        const auto address = std::span(m_buffer).subspan(addressOffset, addressSize);
        host = ipv6Address(std::span<const std::byte, 16>(address.data(), 16));
    }
    else
    {
        host.assign(reinterpret_cast<const char *>(m_buffer.data() + addressOffset), addressSize);
        if (!validDomain(host))
        {
            m_state = State::Failed;
            m_buffer.clear();
            return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                         .response = reply(Socks5ReplyCode::AddressTypeNotSupported)};
        }
    }
    const std::uint16_t port =
        static_cast<std::uint16_t>((byteValue(m_buffer[frameSize - 2]) << 8U) | byteValue(m_buffer[frameSize - 1]));
    if (port == 0)
    {
        m_state = State::Failed;
        m_buffer.clear();
        return Socks5HandshakeResult{.status = Socks5HandshakeStatus::Rejected,
                                     .response = reply(Socks5ReplyCode::ConnectionNotAllowed)};
    }

    Socks5HandshakeResult result{.status = Socks5HandshakeStatus::DestinationReady,
                                 .destination = Socks5Destination{.host = std::move(host), .port = port}};
    result.remainingData.assign(m_buffer.begin() + static_cast<std::ptrdiff_t>(frameSize), m_buffer.end());
    m_buffer.clear();
    m_state = State::Complete;
    return result;
}

std::vector<std::byte> Socks5Handshake::reply(const Socks5ReplyCode code)
{
    return bytes({0x05U, static_cast<std::uint8_t>(code), 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U});
}

} // namespace ztermy::forwarding
