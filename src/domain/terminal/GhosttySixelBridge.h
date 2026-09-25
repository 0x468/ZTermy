#pragma once

#include "domain/terminal/SixelDecoder.h"
#include "domain/terminal/TerminalEngine.h"
#include "domain/terminal/TerminalGraphicsStream.h"

#include <ghostty/vt.h>

#include <optional>

namespace ztermy::terminal
{
// Adapts Sixel and its geometry queries to the same pinned storage used by Kitty.
class GhosttySixelBridge final : private TerminalGraphicsSink
{
public:
    GhosttySixelBridge(GhosttyTerminal terminal, GhosttyTerminalWritePtyFn reply, void *userdata) noexcept
        : m_terminal(terminal), m_reply(reply), m_userdata(userdata)
    {
    }
    void feed(std::string_view bytes, TerminalGeometry geometry);

private:
    void writeTerminal(std::string_view bytes) override;
    void beginSixel(std::string_view parameters) override;
    void writeSixel(std::string_view bytes) override;
    void endSixel(bool cancelled) override;
    void controlSequence(std::string_view parameters) override;
    void resetTerminal() override;
    void display(const SixelImage &image);
    void graphicsQuery(std::string_view parameters);

    GhosttyTerminal m_terminal;
    GhosttyTerminalWritePtyFn m_reply;
    void *m_userdata;
    TerminalGeometry m_geometry;
    TerminalGraphicsStream m_stream;
    std::optional<SixelDecoder> m_decoder;
    bool m_absolute = false, m_cursorRight = false;
    bool m_savedAbsolute = false, m_savedCursorRight = false;
};
} // namespace ztermy::terminal
