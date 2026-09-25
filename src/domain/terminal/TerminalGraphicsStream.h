#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ztermy::terminal
{
class TerminalGraphicsSink
{
public:
    virtual ~TerminalGraphicsSink() = default;
    virtual void writeTerminal(std::string_view bytes) = 0;
    virtual void beginSixel(std::string_view parameters) = 0;
    virtual void writeSixel(std::string_view bytes) = 0;
    virtual void endSixel(bool cancelled) = 0;
    // Observations follow forwarding; parameters include the CSI final byte,
    // but not the introducer. Oversized/incomplete sequences are not reported.
    virtual void controlSequence(std::string_view) {}
    virtual void resetTerminal() {}
};

// Separates Sixel DCS from the VT stream without buffering image payloads.
// All callback views are borrowed for the duration of the call only.
class TerminalGraphicsStream final
{
public:
    void append(std::string_view bytes, TerminalGraphicsSink &sink);
    void finish(TerminalGraphicsSink &sink);

private:
    enum class State : std::uint8_t
    {
        ground,
        escape,
        csi,
        header,
        string,
        stringEscape,
        sixel,
        sixelEscape
    };
    void escaped(char byte, TerminalGraphicsSink &sink);
    void forwardHeader(TerminalGraphicsSink &sink);
    void trackUtf8(std::uint8_t byte);

    State m_state = State::ground;
    std::array<char, 128> m_header{};
    std::size_t m_headerSize = 0;
    std::size_t m_prefixSize = 0;
    std::uint8_t m_utf8Remaining = 0;
    bool m_osc = false;
    bool m_sixelCandidate = true;
    bool m_controlOverflow = false;
};
} // namespace ztermy::terminal
