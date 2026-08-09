#pragma once

#include "domain/workbench/ScriptDefinition.h"

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ztermy::workbench
{

enum class ScriptExecutionState : std::uint8_t
{
    idle,
    running,
    waitingForOutput,
    completed,
    cancelled,
    timedOut,
};

enum class ScriptExecutionError : std::uint8_t
{
    alreadyActive,
    invalidScript,
    invalidTarget,
};

struct ScriptExecutionSnapshot final
{
    ScriptExecutionState state = ScriptExecutionState::idle;
    std::string scriptId;
    std::string scriptName;
    std::string targetId;
    std::size_t dispatchedSteps = 0;
    std::size_t totalSteps = 0;

    bool operator==(const ScriptExecutionSnapshot &) const = default;
};

class ScriptExecution final
{
public:
    using TimePoint = std::chrono::milliseconds;

    [[nodiscard]] std::expected<std::vector<std::string>, ScriptExecutionError>
    start(RenderedScript script, std::string targetId, TimePoint now);
    [[nodiscard]] std::vector<std::string> observeOutput(std::span<const std::byte> bytes, TimePoint now);
    [[nodiscard]] bool tick(TimePoint now) noexcept;
    [[nodiscard]] bool cancel() noexcept;

    [[nodiscard]] ScriptExecutionSnapshot snapshot() const;
    [[nodiscard]] bool active() const noexcept;

private:
    [[nodiscard]] std::vector<std::string> dispatchReadySteps(TimePoint now);

    ScriptExecutionState m_state = ScriptExecutionState::idle;
    RenderedScript m_script;
    std::string m_targetId;
    std::size_t m_nextStep = 0;
    std::string m_outputWindow;
    TimePoint m_deadline{};
};

} // namespace ztermy::workbench
