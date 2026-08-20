#pragma once

#include "domain/ai/AiProviderTypes.h"

namespace ztermy::ai
{

struct AiProviderRecoveryPlan final
{
    bool retryAvailable = false;
    bool settingsAvailable = false;
    bool newConversationAvailable = false;

    [[nodiscard]] friend bool operator==(const AiProviderRecoveryPlan &, const AiProviderRecoveryPlan &) = default;
};

class AiProviderRecoveryPolicy final
{
public:
    [[nodiscard]] static AiProviderRecoveryPlan plan(AiProviderErrorCode code) noexcept;
};

} // namespace ztermy::ai
