#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ztermy::terminal
{

enum class ShellIntegrationProtocol : std::uint8_t
{
    none,
    osc133,
    osc633,
    osc1337,
};

enum class ShellIntegrationEventType : std::uint8_t
{
    output,
    promptStart,
    commandStart,
    commandExecuted,
    commandFinished,
    explicitCommand,
    workingDirectory,
    richCapabilityClaim,
    decoderError,
};

struct ShellIntegrationEvent final
{
    ShellIntegrationEventType type = ShellIntegrationEventType::output;
    ShellIntegrationProtocol protocol = ShellIntegrationProtocol::none;
    std::uint64_t outputStreamOffset = 0;
    std::uint64_t rawStreamOffset = 0;
    std::vector<std::byte> output;
    std::string value;
    std::optional<int> exitStatus;
    bool nonceVerified = false;
};

struct ShellIntegrationDecoderLimits final
{
    std::size_t maxOscPayloadBytes = std::size_t{16} * 1024;
};

class ShellIntegrationDecoder final
{
public:
    explicit ShellIntegrationDecoder(std::string expectedNonce = {}, ShellIntegrationDecoderLimits limits = {});

    [[nodiscard]] std::vector<ShellIntegrationEvent> append(std::span<const std::byte> bytes);
    [[nodiscard]] std::vector<ShellIntegrationEvent> finish();
    [[nodiscard]] std::uint64_t rawStreamOffset() const noexcept;
    [[nodiscard]] std::uint64_t outputStreamOffset() const noexcept;

private:
    enum class State : std::uint8_t
    {
        normal,
        escape,
        osc,
        oscEscape,
    };

    void emitOutputByte(std::vector<ShellIntegrationEvent> &events, std::byte value, std::uint64_t rawOffset);
    void finishOsc(std::vector<ShellIntegrationEvent> &events, std::uint64_t rawEndOffset);
    void parseOsc(std::vector<ShellIntegrationEvent> &events, std::uint64_t rawEndOffset) const;

    std::string m_expectedNonce;
    ShellIntegrationDecoderLimits m_limits;
    State m_state = State::normal;
    std::string m_oscPayload;
    std::uint64_t m_rawStreamOffset = 0;
    std::uint64_t m_outputStreamOffset = 0;
    std::uint64_t m_pendingEscapeRawOffset = 0;
    std::uint64_t m_oscRawOffset = 0;
    bool m_oscOverflow = false;
};

} // namespace ztermy::terminal
