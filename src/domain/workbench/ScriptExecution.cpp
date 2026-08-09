#include "domain/workbench/ScriptExecution.h"

#include <algorithm>
#include <utility>

namespace ztermy::workbench
{

std::expected<std::vector<std::string>, ScriptExecutionError>
ScriptExecution::start(RenderedScript script, std::string targetId, const TimePoint now)
{
    if (active())
    {
        return std::unexpected(ScriptExecutionError::alreadyActive);
    }
    if (script.id.empty() || script.name.empty() || script.steps.empty()
        || std::ranges::any_of(script.steps, [](const RenderedScriptStep &step) {
               return step.command.empty()
                      || (step.continuation == ScriptContinuation::literalOutput && step.outputMarker.empty());
           }))
    {
        return std::unexpected(ScriptExecutionError::invalidScript);
    }
    if (targetId.empty())
    {
        return std::unexpected(ScriptExecutionError::invalidTarget);
    }
    m_script = std::move(script);
    m_targetId = std::move(targetId);
    m_nextStep = 0;
    m_outputWindow.clear();
    m_deadline = {};
    m_state = ScriptExecutionState::running;
    return dispatchReadySteps(now);
}

std::vector<std::string> ScriptExecution::observeOutput(const std::span<const std::byte> bytes, const TimePoint now)
{
    if (m_state != ScriptExecutionState::waitingForOutput || bytes.empty())
    {
        return {};
    }
    const std::size_t bytesToKeep = (std::min)(bytes.size(), maximumOutputMatchWindowBytes);
    if (bytesToKeep == maximumOutputMatchWindowBytes)
    {
        const auto *begin = reinterpret_cast<const char *>(bytes.data() + (bytes.size() - bytesToKeep));
        m_outputWindow.assign(begin, bytesToKeep);
    }
    else
    {
        const std::size_t overflow = m_outputWindow.size() + bytesToKeep > maximumOutputMatchWindowBytes
                                         ? m_outputWindow.size() + bytesToKeep - maximumOutputMatchWindowBytes
                                         : 0;
        if (overflow > 0)
        {
            m_outputWindow.erase(0, overflow);
        }
        m_outputWindow.append(reinterpret_cast<const char *>(bytes.data()), bytesToKeep);
    }
    const RenderedScriptStep &waitingStep = m_script.steps[m_nextStep - 1];
    if (m_outputWindow.find(waitingStep.outputMarker) == std::string::npos)
    {
        static_cast<void>(tick(now));
        return {};
    }
    m_outputWindow.clear();
    m_state = ScriptExecutionState::running;
    return dispatchReadySteps(now);
}

bool ScriptExecution::tick(const TimePoint now) noexcept
{
    if (m_state != ScriptExecutionState::waitingForOutput || now < m_deadline)
    {
        return false;
    }
    m_state = ScriptExecutionState::timedOut;
    m_outputWindow.clear();
    return true;
}

bool ScriptExecution::cancel() noexcept
{
    if (!active())
    {
        return false;
    }
    m_state = ScriptExecutionState::cancelled;
    m_outputWindow.clear();
    return true;
}

ScriptExecutionSnapshot ScriptExecution::snapshot() const
{
    return {.state = m_state,
            .scriptId = m_script.id,
            .scriptName = m_script.name,
            .targetId = m_targetId,
            .dispatchedSteps = m_nextStep,
            .totalSteps = m_script.steps.size()};
}

bool ScriptExecution::active() const noexcept
{
    return m_state == ScriptExecutionState::running || m_state == ScriptExecutionState::waitingForOutput;
}

std::vector<std::string> ScriptExecution::dispatchReadySteps(const TimePoint now)
{
    std::vector<std::string> commands;
    while (m_nextStep < m_script.steps.size())
    {
        const RenderedScriptStep &step = m_script.steps[m_nextStep];
        commands.push_back(step.command);
        ++m_nextStep;
        if (step.continuation == ScriptContinuation::literalOutput)
        {
            m_state = ScriptExecutionState::waitingForOutput;
            m_deadline = now + std::chrono::milliseconds(step.timeoutMs);
            return commands;
        }
    }
    m_state = ScriptExecutionState::completed;
    return commands;
}

} // namespace ztermy::workbench
