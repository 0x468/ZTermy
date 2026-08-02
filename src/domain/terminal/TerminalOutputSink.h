#pragma once

#include <cstddef>
#include <span>

namespace ztermy::terminal
{

class TerminalOutputSink
{
public:
    virtual ~TerminalOutputSink() = default;

    TerminalOutputSink(const TerminalOutputSink &) = delete;
    TerminalOutputSink &operator=(const TerminalOutputSink &) = delete;

    virtual void append(std::span<const std::byte> bytes) noexcept = 0;

protected:
    TerminalOutputSink() = default;
};

} // namespace ztermy::terminal
