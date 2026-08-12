#include "domain/ai/AiTerminalFrameTracker.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <string_view>

namespace ztermy::ai
{
namespace
{
constexpr std::size_t maximumLines = 300;
constexpr std::size_t maximumLineBytes = 4096;
constexpr std::size_t maximumEscapeTailBytes = 64;

[[nodiscard]] std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void bound(AiTerminalFrameInput &frame)
{
    if (frame.lines.size() > maximumLines)
    {
        frame.lines.erase(frame.lines.begin(), frame.lines.end() - static_cast<std::ptrdiff_t>(maximumLines));
    }
    for (std::string &line : frame.lines)
    {
        if (line.size() > maximumLineBytes)
        {
            line.resize(maximumLineBytes);
        }
    }
}

[[nodiscard]] std::size_t latest(const std::string &value, const std::initializer_list<std::string_view> needles)
{
    std::size_t result = std::string::npos;
    for (const std::string_view needle : needles)
    {
        const std::size_t found = value.rfind(needle);
        if (found != std::string::npos && (result == std::string::npos || found > result))
        {
            result = found;
        }
    }
    return result;
}
} // namespace

AiTerminalFrameTracker::AiTerminalFrameTracker(Clock clock) : m_clock(clock ? std::move(clock) : Clock{systemNow}) {}

void AiTerminalFrameTracker::observeFrame(AiTerminalFrameInput frame)
{
    bound(frame);
    const std::int64_t timestamp = now();
    std::scoped_lock lock(m_mutex);
    const bool geometryChanged = frame.columns != m_current.columns || frame.rows != m_current.rows;
    const bool cursorChanged = frame.cursorColumn != m_current.cursorColumn || frame.cursorRow != m_current.cursorRow
                               || frame.cursorVisible != m_current.cursorVisible;
    m_changedLines.clear();
    const std::size_t maximum = std::max(frame.lines.size(), m_current.lines.size());
    for (std::size_t index = 0; index < maximum; ++index)
    {
        if (index >= frame.lines.size() || index >= m_current.lines.size()
            || frame.lines[index] != m_current.lines[index])
        {
            m_changedLines.push_back(index);
        }
    }
    if (m_revision == 0 || geometryChanged || cursorChanged || !m_changedLines.empty())
    {
        m_current = std::move(frame);
        m_lastChangeRequiresFull = m_revision == 0 || geometryChanged;
        markChanged(timestamp);
    }
}

void AiTerminalFrameTracker::observeOutput(const std::span<const std::byte> bytes) noexcept
{
    try
    {
        std::string incoming;
        incoming.reserve(bytes.size());
        for (const std::byte value : bytes)
        {
            incoming.push_back(static_cast<char>(value));
        }
        std::scoped_lock lock(m_mutex);
        m_escapeTail.append(incoming);
        const std::size_t enter = latest(m_escapeTail, {"\x1b[?47h", "\x1b[?1047h", "\x1b[?1049h"});
        const std::size_t leave = latest(m_escapeTail, {"\x1b[?47l", "\x1b[?1047l", "\x1b[?1049l"});
        const bool next = enter == std::string::npos && leave == std::string::npos
                              ? m_alternateScreen
                              : leave == std::string::npos || (enter != std::string::npos && enter > leave);
        if (next != m_alternateScreen)
        {
            m_alternateScreen = next;
            m_changedLines.clear();
            m_lastChangeRequiresFull = false;
            markChanged(now());
        }
        if (m_escapeTail.size() > maximumEscapeTailBytes)
        {
            m_escapeTail.erase(0, m_escapeTail.size() - maximumEscapeTailBytes);
        }
    }
    catch (...)
    {
        static_cast<void>(m_droppedOutputObservations.fetch_add(1, std::memory_order_relaxed));
    }
}

AiTerminalFrameDelta AiTerminalFrameTracker::snapshot(const std::uint64_t afterRevision) const
{
    const std::int64_t timestamp = now();
    std::scoped_lock lock(m_mutex);
    AiTerminalFrameDelta result{.revision = m_revision,
                                .baseRevision = afterRevision,
                                .changedUtcMs = m_changedUtcMs,
                                .idleMilliseconds = std::max<std::int64_t>(0, timestamp - m_changedUtcMs),
                                .columns = m_current.columns,
                                .rows = m_current.rows,
                                .cursorColumn = m_current.cursorColumn,
                                .cursorRow = m_current.cursorRow,
                                .cursorVisible = m_current.cursorVisible,
                                .alternateScreen = m_alternateScreen,
                                .droppedOutputObservations =
                                    m_droppedOutputObservations.load(std::memory_order_relaxed)};
    if (afterRevision == m_revision)
    {
        result.full = false;
        return result;
    }
    const bool canDelta =
        afterRevision > 0 && m_revision > 0 && afterRevision + 1 == m_revision && !m_lastChangeRequiresFull;
    result.full = !canDelta;
    result.cursorExpired = afterRevision > 0 && afterRevision + 1 < m_revision;
    if (canDelta)
    {
        result.lines.reserve(m_changedLines.size());
        for (const std::size_t index : m_changedLines)
        {
            result.lines.push_back(
                AiTerminalFrameLine{.index = index,
                                    .text = index < m_current.lines.size() ? m_current.lines[index] : ""});
        }
        return result;
    }
    result.lines.reserve(m_current.lines.size());
    for (std::size_t index = 0; index < m_current.lines.size(); ++index)
    {
        result.lines.push_back(AiTerminalFrameLine{.index = index, .text = m_current.lines[index]});
    }
    return result;
}

std::int64_t AiTerminalFrameTracker::now() const
{
    return m_clock();
}

void AiTerminalFrameTracker::markChanged(const std::int64_t timestamp)
{
    ++m_revision;
    m_changedUtcMs = timestamp;
}

} // namespace ztermy::ai
