#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace ztermy::ai
{

struct AiTerminalFrameInput final
{
    std::vector<std::string> lines;
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    std::uint16_t cursorColumn = 0;
    std::uint16_t cursorRow = 0;
    bool cursorVisible = false;
};

struct AiTerminalFrameLine final
{
    std::size_t index = 0;
    std::string text;
};

struct AiTerminalFrameDelta final
{
    std::uint64_t revision = 0;
    std::uint64_t baseRevision = 0;
    std::int64_t changedUtcMs = 0;
    std::int64_t idleMilliseconds = 0;
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    std::uint16_t cursorColumn = 0;
    std::uint16_t cursorRow = 0;
    bool cursorVisible = false;
    bool alternateScreen = false;
    bool full = true;
    bool cursorExpired = false;
    std::vector<AiTerminalFrameLine> lines;
};

class AiTerminalFrameTracker final
{
public:
    using Clock = std::function<std::int64_t()>;

    explicit AiTerminalFrameTracker(Clock clock = {});

    void observeFrame(AiTerminalFrameInput frame);
    void observeOutput(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] AiTerminalFrameDelta snapshot(std::uint64_t afterRevision = 0) const;

private:
    [[nodiscard]] std::int64_t now() const;
    void markChanged(std::int64_t timestamp);

    mutable std::mutex m_mutex;
    Clock m_clock;
    AiTerminalFrameInput m_current;
    std::vector<std::size_t> m_changedLines;
    std::string m_escapeTail;
    std::uint64_t m_revision = 0;
    std::int64_t m_changedUtcMs = 0;
    bool m_alternateScreen = false;
    bool m_lastChangeRequiresFull = true;
};

} // namespace ztermy::ai
