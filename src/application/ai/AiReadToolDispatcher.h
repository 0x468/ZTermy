#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiReadTools.h"

#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

class AiReadToolDispatcher final
{
public:
    explicit AiReadToolDispatcher(AiReadTools tools = AiReadTools{});

    [[nodiscard]] static std::vector<AiToolDefinition> definitions();
    [[nodiscard]] std::string execute(std::string_view toolName, std::string_view argumentsJson,
                                      const AiTerminalReadSnapshot &session) const;

private:
    AiReadTools m_tools;
};

} // namespace ztermy::ai
