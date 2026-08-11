#include "domain/terminal/ShellIntegrationDecoder.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

namespace ztermy::terminal
{

namespace
{

constexpr std::byte escapeByte{0x1b};
constexpr std::byte bellByte{0x07};

[[nodiscard]] char asChar(const std::byte value) noexcept
{
    return static_cast<char>(std::to_integer<unsigned char>(value));
}

[[nodiscard]] bool startsWith(const std::string_view value, const std::string_view prefix) noexcept
{
    return value.starts_with(prefix);
}

[[nodiscard]] std::optional<int> parseInteger(const std::string_view value) noexcept
{
    if (value.empty())
    {
        return std::nullopt;
    }
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<unsigned char> hexByte(const char high, const char low) noexcept
{
    const auto nibble = [](const char value) -> std::optional<unsigned char> {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned char>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned char>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F')
        {
            return static_cast<unsigned char>(value - 'A' + 10);
        }
        return std::nullopt;
    };

    const auto left = nibble(high);
    const auto right = nibble(low);
    if (!left || !right)
    {
        return std::nullopt;
    }
    return static_cast<unsigned char>((*left << 4U) | *right);
}

[[nodiscard]] std::optional<std::string> decodeCommandLine(const std::string_view encoded)
{
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index)
    {
        if (encoded[index] != '\\')
        {
            decoded.push_back(encoded[index]);
            continue;
        }
        if (index + 1 >= encoded.size())
        {
            return std::nullopt;
        }
        if (encoded[index + 1] == '\\')
        {
            decoded.push_back('\\');
            ++index;
            continue;
        }
        if (encoded[index + 1] != 'x' || index + 3 >= encoded.size())
        {
            return std::nullopt;
        }
        const auto parsed = hexByte(encoded[index + 2], encoded[index + 3]);
        if (!parsed)
        {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(*parsed));
        index += 3;
    }
    return decoded;
}

[[nodiscard]] ShellIntegrationEvent event(const ShellIntegrationEventType type, const ShellIntegrationProtocol protocol,
                                          const std::uint64_t outputOffset, const std::uint64_t rawOffset)
{
    return ShellIntegrationEvent{
        .type = type,
        .protocol = protocol,
        .outputStreamOffset = outputOffset,
        .rawStreamOffset = rawOffset,
    };
}

} // namespace

ShellIntegrationDecoder::ShellIntegrationDecoder(std::string expectedNonce, ShellIntegrationDecoderLimits limits)
    : m_expectedNonce(std::move(expectedNonce)), m_limits(limits)
{
    m_limits.maxOscPayloadBytes = std::max<std::size_t>(1, m_limits.maxOscPayloadBytes);
    m_oscPayload.reserve(std::min<std::size_t>(m_limits.maxOscPayloadBytes, 256));
}

std::vector<ShellIntegrationEvent> ShellIntegrationDecoder::append(const std::span<const std::byte> bytes)
{
    std::vector<ShellIntegrationEvent> events;
    for (const std::byte value : bytes)
    {
        const std::uint64_t rawOffset = m_rawStreamOffset++;
        switch (m_state)
        {
            case State::normal:
                if (value == escapeByte)
                {
                    m_state = State::escape;
                    m_pendingEscapeRawOffset = rawOffset;
                }
                else
                {
                    emitOutputByte(events, value, rawOffset);
                }
                break;
            case State::escape:
                if (asChar(value) == ']')
                {
                    m_state = State::osc;
                    m_oscRawOffset = m_pendingEscapeRawOffset;
                    m_oscPayload.clear();
                    m_oscOverflow = false;
                }
                else
                {
                    emitOutputByte(events, escapeByte, m_pendingEscapeRawOffset);
                    if (value == escapeByte)
                    {
                        m_pendingEscapeRawOffset = rawOffset;
                    }
                    else
                    {
                        emitOutputByte(events, value, rawOffset);
                        m_state = State::normal;
                    }
                }
                break;
            case State::osc:
                if (value == bellByte)
                {
                    finishOsc(events, rawOffset + 1);
                }
                else if (value == escapeByte)
                {
                    m_state = State::oscEscape;
                }
                else if (m_oscPayload.size() < m_limits.maxOscPayloadBytes)
                {
                    m_oscPayload.push_back(asChar(value));
                }
                else
                {
                    m_oscOverflow = true;
                }
                break;
            case State::oscEscape:
                if (asChar(value) == '\\')
                {
                    finishOsc(events, rawOffset + 1);
                }
                else
                {
                    if (m_oscPayload.size() < m_limits.maxOscPayloadBytes)
                    {
                        m_oscPayload.push_back(asChar(escapeByte));
                    }
                    else
                    {
                        m_oscOverflow = true;
                    }
                    if (value == bellByte)
                    {
                        finishOsc(events, rawOffset + 1);
                    }
                    else
                    {
                        if (m_oscPayload.size() < m_limits.maxOscPayloadBytes)
                        {
                            m_oscPayload.push_back(asChar(value));
                        }
                        else
                        {
                            m_oscOverflow = true;
                        }
                        m_state = value == escapeByte ? State::oscEscape : State::osc;
                    }
                }
                break;
        }
    }
    return events;
}

std::vector<ShellIntegrationEvent> ShellIntegrationDecoder::finish()
{
    std::vector<ShellIntegrationEvent> events;
    if (m_state == State::escape)
    {
        emitOutputByte(events, escapeByte, m_pendingEscapeRawOffset);
    }
    else if (m_state == State::osc || m_state == State::oscEscape)
    {
        auto failure = event(ShellIntegrationEventType::decoderError, ShellIntegrationProtocol::none,
                             m_outputStreamOffset, m_oscRawOffset);
        failure.value = "incomplete-osc";
        events.push_back(std::move(failure));
    }
    m_state = State::normal;
    m_oscPayload.clear();
    m_oscOverflow = false;
    return events;
}

std::uint64_t ShellIntegrationDecoder::rawStreamOffset() const noexcept
{
    return m_rawStreamOffset;
}

std::uint64_t ShellIntegrationDecoder::outputStreamOffset() const noexcept
{
    return m_outputStreamOffset;
}

void ShellIntegrationDecoder::emitOutputByte(std::vector<ShellIntegrationEvent> &events, const std::byte value,
                                             const std::uint64_t rawOffset)
{
    if (!events.empty() && events.back().type == ShellIntegrationEventType::output
        && events.back().outputStreamOffset + events.back().output.size() == m_outputStreamOffset)
    {
        events.back().output.push_back(value);
    }
    else
    {
        auto output =
            event(ShellIntegrationEventType::output, ShellIntegrationProtocol::none, m_outputStreamOffset, rawOffset);
        output.output.push_back(value);
        events.push_back(std::move(output));
    }
    ++m_outputStreamOffset;
}

void ShellIntegrationDecoder::finishOsc(std::vector<ShellIntegrationEvent> &events, const std::uint64_t rawEndOffset)
{
    if (m_oscOverflow)
    {
        auto failure = event(ShellIntegrationEventType::decoderError, ShellIntegrationProtocol::none,
                             m_outputStreamOffset, m_oscRawOffset);
        failure.value = "osc-payload-limit";
        events.push_back(std::move(failure));
    }
    else
    {
        parseOsc(events, rawEndOffset);
    }
    m_state = State::normal;
    m_oscPayload.clear();
    m_oscOverflow = false;
}

void ShellIntegrationDecoder::parseOsc(std::vector<ShellIntegrationEvent> &events,
                                       const std::uint64_t rawEndOffset) const
{
    const std::string_view payload = m_oscPayload;
    ShellIntegrationProtocol protocol = ShellIntegrationProtocol::none;
    std::string_view body;
    if (startsWith(payload, "133;"))
    {
        protocol = ShellIntegrationProtocol::osc133;
        body = payload.substr(4);
    }
    else if (startsWith(payload, "633;"))
    {
        protocol = ShellIntegrationProtocol::osc633;
        body = payload.substr(4);
    }
    else if (startsWith(payload, "1337;CurrentDir="))
    {
        auto cwd = event(ShellIntegrationEventType::workingDirectory, ShellIntegrationProtocol::osc1337,
                         m_outputStreamOffset, m_oscRawOffset);
        cwd.value = std::string(payload.substr(std::string_view("1337;CurrentDir=").size()));
        events.push_back(std::move(cwd));
        return;
    }
    else
    {
        return;
    }

    const auto emitMarker = [&](const ShellIntegrationEventType type) {
        events.push_back(event(type, protocol, m_outputStreamOffset, m_oscRawOffset));
    };
    if (body == "A")
    {
        emitMarker(ShellIntegrationEventType::promptStart);
    }
    else if (body == "B")
    {
        emitMarker(ShellIntegrationEventType::commandStart);
    }
    else if (body == "C")
    {
        emitMarker(ShellIntegrationEventType::commandExecuted);
    }
    else if (body == "D" || startsWith(body, "D;"))
    {
        auto finished =
            event(ShellIntegrationEventType::commandFinished, protocol, m_outputStreamOffset, m_oscRawOffset);
        if (body.size() > 2)
        {
            finished.exitStatus = parseInteger(body.substr(2));
            if (!finished.exitStatus)
            {
                finished.type = ShellIntegrationEventType::decoderError;
                finished.value = "invalid-exit-status";
            }
        }
        events.push_back(std::move(finished));
    }
    else if (protocol == ShellIntegrationProtocol::osc633 && startsWith(body, "E;"))
    {
        const std::string_view encodedAndNonce = body.substr(2);
        const std::size_t separator = encodedAndNonce.find(';');
        const std::string_view encoded = encodedAndNonce.substr(0, separator);
        const auto decoded = decodeCommandLine(encoded);
        if (!decoded)
        {
            auto failure =
                event(ShellIntegrationEventType::decoderError, protocol, m_outputStreamOffset, m_oscRawOffset);
            failure.value = "invalid-command-escape";
            events.push_back(std::move(failure));
            return;
        }
        auto command =
            event(ShellIntegrationEventType::explicitCommand, protocol, m_outputStreamOffset, m_oscRawOffset);
        command.value = *decoded;
        if (separator != std::string_view::npos)
        {
            const std::string_view nonce = encodedAndNonce.substr(separator + 1);
            command.nonceVerified = !m_expectedNonce.empty() && nonce == m_expectedNonce;
        }
        events.push_back(std::move(command));
    }
    else if (protocol == ShellIntegrationProtocol::osc633 && startsWith(body, "P;"))
    {
        const std::string_view property = body.substr(2);
        if (startsWith(property, "Cwd="))
        {
            auto cwd =
                event(ShellIntegrationEventType::workingDirectory, protocol, m_outputStreamOffset, m_oscRawOffset);
            cwd.value = std::string(property.substr(4));
            events.push_back(std::move(cwd));
        }
        else if (property == "HasRichCommandDetection=True")
        {
            emitMarker(ShellIntegrationEventType::richCapabilityClaim);
        }
    }

    (void)rawEndOffset;
}

} // namespace ztermy::terminal
