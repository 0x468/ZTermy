#pragma once

#include "domain/ai/AiContextBroker.h"
#include "domain/ai/AiProviderTypes.h"

#include <string>

namespace ztermy::ai
{

struct AiSerializedContext final
{
    std::string text;
    std::size_t itemCount = 0;
    std::size_t sourceBytes = 0;
    std::size_t estimatedTokens = 0;
    std::size_t redactionCount = 0;
    bool truncated = false;
};

class AiContextSerializer final
{
public:
    [[nodiscard]] static AiSerializedContext serialize(const AiContextBundle &bundle);
    [[nodiscard]] static AiChatMessage asUntrustedEvidenceMessage(const AiContextBundle &bundle);
};

} // namespace ztermy::ai
