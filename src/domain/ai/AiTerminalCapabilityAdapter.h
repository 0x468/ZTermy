#pragma once

#include "domain/terminal/CommandBlockStore.h"

#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiTerminalInteractionCapability final
{
    std::string shellFamily;
    std::string semanticQuality;
    std::string observationMode;
    std::string degradedReason;
    bool exactCommandBoundaries = false;
    bool reliableExitStatus = false;
    bool frameDeltas = true;
};

class AiTerminalCapabilityAdapter final
{
public:
    [[nodiscard]] static AiTerminalInteractionCapability
    describe(std::string_view shell, terminal::TerminalSemanticCapability semanticCapability, bool alternateScreen);
};

} // namespace ztermy::ai
