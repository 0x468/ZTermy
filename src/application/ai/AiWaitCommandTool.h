#pragma once

#include "domain/ai/AiCommandTracker.h"
#include "domain/ai/AiProviderTypes.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiWaitCommandRequest final
{
    std::string commandId;
    AiSessionTarget target;
    std::uint32_t timeoutMilliseconds = 30'000;
};

class AiWaitCommandTool final
{
public:
    [[nodiscard]] static AiToolDefinition definition();
    [[nodiscard]] static std::expected<AiWaitCommandRequest, std::string> parse(std::string_view argumentsJson);
    [[nodiscard]] static std::string result(const AiTrackedCommand &command);
    [[nodiscard]] static std::string timeout(const AiTrackedCommand &command);
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
};

} // namespace ztermy::ai
