#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ztermy::workbench
{

enum class ScriptRecorderState : std::uint8_t
{
    Idle,
    Recording,
    Paused,
    Review,
};

enum class RecordedScriptStepKind : std::uint8_t
{
    Send,
    Delay,
};

struct RecordedScriptStep final
{
    RecordedScriptStepKind kind = RecordedScriptStepKind::Send;
    std::string command;
    std::uint32_t delayMilliseconds = 0;

    friend bool operator==(const RecordedScriptStep &, const RecordedScriptStep &) = default;
};

inline constexpr std::size_t maximumRecordedScriptSteps = 512;
inline constexpr std::size_t maximumRecordedCommandBytes = std::size_t{64} * 1024;

class ScriptRecorder final
{
public:
    using TimePoint = std::chrono::milliseconds;

    [[nodiscard]] ScriptRecorderState state() const noexcept;
    [[nodiscard]] const std::vector<RecordedScriptStep> &steps() const noexcept;

    bool start(TimePoint now);
    bool pause();
    bool resume(TimePoint now);
    bool stop();
    void clear() noexcept;

    // Only structured command actions call this method. Raw terminal keyboard
    // input is deliberately outside this API so password prompts cannot be
    // captured by the recorder.
    [[nodiscard]] bool recordCommand(std::string command, TimePoint now);

private:
    ScriptRecorderState m_state = ScriptRecorderState::Idle;
    std::vector<RecordedScriptStep> m_steps;
    TimePoint m_lastCommandAt{};
    bool m_hasLastCommand = false;
};

} // namespace ztermy::workbench
