#include "domain/terminal/TerminalGraphicsStream.h"

namespace ztermy::terminal
{
void TerminalGraphicsStream::trackUtf8(std::uint8_t byte)
{
    if (m_utf8Remaining && byte >= 0x80 && byte <= 0xbf)
    {
        --m_utf8Remaining;
        return;
    }
    m_utf8Remaining = byte >= 0xc2 && byte <= 0xdf   ? 1
                      : byte >= 0xe0 && byte <= 0xef ? 2
                      : byte >= 0xf0 && byte <= 0xf4 ? 3
                                                     : 0;
}

void TerminalGraphicsStream::forwardHeader(TerminalGraphicsSink &sink)
{
    sink.writeTerminal({m_header.data(), m_headerSize});
    m_headerSize = 0;
}

void TerminalGraphicsStream::escaped(char byte, TerminalGraphicsSink &sink)
{
    if (byte == 'P')
    {
        m_header[0] = '\x1b';
        m_header[1] = 'P';
        m_headerSize = m_prefixSize = 2;
        m_sixelCandidate = true;
        m_state = State::header;
    }
    else if (byte == '\x1b')
    {
        sink.writeTerminal("\x1b");
        m_state = State::escape;
    }
    else
    {
        const std::array sequence{'\x1b', byte};
        sink.writeTerminal({sequence.data(), sequence.size()});
        m_osc = byte == ']';
        m_state = m_osc || byte == '_' || byte == '^' || byte == 'X' ? State::string : State::ground;
    }
}

void TerminalGraphicsStream::append(std::string_view bytes, TerminalGraphicsSink &sink)
{
    // Batch adjacent bytes into borrowed spans. Ordinary output takes one sink
    // call per input chunk, not one terminal-parser invocation per byte.
    std::string_view pending;
    bool pendingSixel = false;
    const auto flush = [&] {
        if (pending.empty())
            return;
        if (pendingSixel)
            sink.writeSixel(pending);
        else
            sink.writeTerminal(pending);
        pending = {};
    };
    const auto emitBytes = [&](std::string_view value, bool sixel = false) {
        if (!pending.empty() && (pendingSixel != sixel || pending.data() + pending.size() != value.data()))
            flush();
        if (pending.empty())
        {
            pending = value;
            pendingSixel = sixel;
        }
        else
            pending = {pending.data(), pending.size() + value.size()};
    };
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const char ch = bytes[index];
        const auto byte = static_cast<std::uint8_t>(ch);
        const bool rawC1 = m_utf8Remaining == 0 && byte >= 0x80 && byte <= 0x9f;
        trackUtf8(byte);
        const auto current = bytes.substr(index, 1);
        switch (m_state)
        {
            case State::ground:
                if (ch == '\x1b')
                {
                    flush();
                    m_state = State::escape;
                }
                else if (rawC1 && byte == 0x90)
                {
                    flush();
                    m_header[0] = ch;
                    m_headerSize = m_prefixSize = 1;
                    m_sixelCandidate = true;
                    m_state = State::header;
                }
                else
                {
                    emitBytes(current);
                    if (rawC1 && (byte == 0x9d || byte == 0x9e || byte == 0x9f || byte == 0x98))
                    {
                        m_osc = byte == 0x9d;
                        m_state = State::string;
                    }
                }
                break;
            case State::escape:
                flush();
                escaped(ch, sink);
                break;
            case State::header:
                flush();
                if (ch == '\x1b' || ch == '\x18' || ch == '\x1a' || (rawC1 && byte == 0x9c))
                {
                    forwardHeader(sink);
                    if (ch == '\x1b')
                        m_state = State::escape;
                    else
                    {
                        sink.writeTerminal(current);
                        m_state = State::ground;
                    }
                    break;
                }
                if (ch >= '@' && ch <= '~')
                {
                    if (ch == 'q' && m_sixelCandidate)
                    {
                        sink.beginSixel({m_header.data() + m_prefixSize, m_headerSize - m_prefixSize});
                        m_headerSize = 0;
                        m_state = State::sixel;
                    }
                    else
                    {
                        forwardHeader(sink);
                        sink.writeTerminal(current);
                        m_state = State::string;
                        m_osc = false;
                    }
                    break;
                }
                if (!((ch >= '0' && ch <= '9') || ch == ';'))
                    m_sixelCandidate = false;
                m_header[m_headerSize++] = ch;
                if (m_headerSize == m_header.size())
                {
                    forwardHeader(sink);
                    m_state = State::string;
                    m_osc = false;
                }
                break;
            case State::string:
                if (ch == '\x1b')
                {
                    flush();
                    m_state = State::stringEscape;
                }
                else
                {
                    emitBytes(current);
                    if ((rawC1 && byte == 0x9c) || ch == '\x18' || ch == '\x1a' || (m_osc && ch == '\x07'))
                        m_state = State::ground;
                }
                break;
            case State::stringEscape:
            {
                flush();
                // Preserve malformed/non-Sixel control strings verbatim. Do not
                // reinterpret an embedded ESC P as a second image payload.
                const std::array sequence{'\x1b', ch};
                sink.writeTerminal({sequence.data(), sequence.size()});
                if (ch == 'P' || ch == ']' || ch == '_' || ch == '^' || ch == 'X')
                {
                    m_osc = ch == ']';
                    m_state = State::string;
                }
                else
                    m_state = State::ground;
                break;
            }
            case State::sixel:
                if (ch == '\x1b')
                {
                    flush();
                    m_state = State::sixelEscape;
                }
                else if ((rawC1 && byte == 0x9c) || ch == '\x18' || ch == '\x1a')
                {
                    flush();
                    sink.endSixel(byte != 0x9c);
                    m_state = State::ground;
                    if (byte != 0x9c)
                        emitBytes(current);
                }
                else
                    emitBytes(current, true);
                break;
            case State::sixelEscape:
                flush();
                sink.endSixel(ch != '\\');
                m_state = State::ground;
                if (ch != '\\')
                    escaped(ch, sink);
                break;
        }
    }
    flush();
}

void TerminalGraphicsStream::finish(TerminalGraphicsSink &sink)
{
    if (m_state == State::sixel || m_state == State::sixelEscape)
        sink.endSixel(true);
    else if (m_state == State::header)
        forwardHeader(sink);
    else if (m_state == State::escape || m_state == State::stringEscape)
        sink.writeTerminal("\x1b");
    m_state = State::ground;
    m_utf8Remaining = 0;
    m_headerSize = 0;
}
} // namespace ztermy::terminal
