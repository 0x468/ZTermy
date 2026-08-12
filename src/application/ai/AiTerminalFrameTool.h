#pragma once

#include "domain/ai/AiCommandTracker.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiTerminalFrameTracker.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

enum class AiTerminalFrameWaitCondition : std::uint8_t
{
    changed,
    idle
};

struct AiTerminalFrameReadRequest final
{
    AiSessionTarget target;
    std::uint64_t afterRevision = 0;
};

struct AiTerminalFrameWaitRequest final
{
    AiSessionTarget target;
    std::uint64_t afterRevision = 0;
    AiTerminalFrameWaitCondition condition = AiTerminalFrameWaitCondition::changed;
    std::uint32_t idleMilliseconds = 0;
    std::uint32_t timeoutMilliseconds = 30'000;
};

class AiTerminalFrameTool final
{
public:
    [[nodiscard]] static AiToolDefinition readDefinition();
    [[nodiscard]] static AiToolDefinition waitDefinition();
    [[nodiscard]] static std::expected<AiTerminalFrameReadRequest, std::string>
    parseRead(std::string_view argumentsJson);
    [[nodiscard]] static std::expected<AiTerminalFrameWaitRequest, std::string>
    parseWait(std::string_view argumentsJson);
    [[nodiscard]] static bool satisfied(const AiTerminalFrameWaitRequest &request,
                                        const AiTerminalFrameDelta &frame) noexcept;
    [[nodiscard]] static std::string result(const AiTerminalFrameDelta &frame,
                                            std::string_view controlOwner = "unclaimed");
    [[nodiscard]] static std::string timeout(const AiTerminalFrameDelta &frame,
                                             std::string_view controlOwner = "unclaimed");
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
};

} // namespace ztermy::ai
