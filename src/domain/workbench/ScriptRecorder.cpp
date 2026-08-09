#include "domain/workbench/ScriptRecorder.h"

#include <algorithm>
#include <utility>

namespace ztermy::workbench
{

ScriptRecorderState ScriptRecorder::state() const noexcept
{
    return m_state;
}

const std::vector<RecordedScriptStep> &ScriptRecorder::steps() const noexcept
{
    return m_steps;
}

bool ScriptRecorder::start(const TimePoint now)
{
    if (m_state == ScriptRecorderState::Recording || m_state == ScriptRecorderState::Paused)
    {
        return false;
    }
    m_steps.clear();
    m_steps.reserve(32);
    m_lastCommandAt = now;
    m_hasLastCommand = false;
    m_state = ScriptRecorderState::Recording;
    return true;
}

bool ScriptRecorder::pause()
{
    if (m_state != ScriptRecorderState::Recording)
    {
        return false;
    }
    m_state = ScriptRecorderState::Paused;
    return true;
}

bool ScriptRecorder::resume(const TimePoint now)
{
    if (m_state != ScriptRecorderState::Paused)
    {
        return false;
    }
    m_lastCommandAt = now;
    m_state = ScriptRecorderState::Recording;
    return true;
}

bool ScriptRecorder::stop()
{
    if (m_state != ScriptRecorderState::Recording && m_state != ScriptRecorderState::Paused)
    {
        return false;
    }
    m_state = ScriptRecorderState::Review;
    return true;
}

void ScriptRecorder::clear() noexcept
{
    m_steps.clear();
    m_hasLastCommand = false;
    m_state = ScriptRecorderState::Idle;
}

bool ScriptRecorder::recordCommand(std::string command, const TimePoint now)
{
    if (m_state != ScriptRecorderState::Recording || command.empty() || command.size() > maximumRecordedCommandBytes)
    {
        return false;
    }
    const std::size_t requiredSteps = m_hasLastCommand && now - m_lastCommandAt >= std::chrono::seconds(1) ? 2 : 1;
    if (m_steps.size() + requiredSteps > maximumRecordedScriptSteps)
    {
        return false;
    }
    if (requiredSteps == 2)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCommandAt);
        const auto bounded = std::clamp<std::int64_t>(elapsed.count(), 1'000, 60'000);
        m_steps.push_back(RecordedScriptStep{.kind = RecordedScriptStepKind::Delay,
                                             .delayMilliseconds = static_cast<std::uint32_t>(bounded)});
    }
    m_steps.push_back(RecordedScriptStep{.kind = RecordedScriptStepKind::Send, .command = std::move(command)});
    m_lastCommandAt = now;
    m_hasLastCommand = true;
    return true;
}

} // namespace ztermy::workbench
