#include "domain/ai/AiProviderRecoveryPolicy.h"

namespace ztermy::ai
{

AiProviderRecoveryPlan AiProviderRecoveryPolicy::plan(const AiProviderErrorCode code) noexcept
{
    switch (code)
    {
        case AiProviderErrorCode::network:
        case AiProviderErrorCode::server:
        case AiProviderErrorCode::cancelled:
            return {.retryAvailable = true};
        case AiProviderErrorCode::authentication:
        case AiProviderErrorCode::rateLimited:
        case AiProviderErrorCode::quotaExceeded:
        case AiProviderErrorCode::invalidRequest:
        case AiProviderErrorCode::protocol:
            return {.retryAvailable = true, .settingsAvailable = true};
        case AiProviderErrorCode::contextOverflow:
            return {.newConversationAvailable = true};
    }
    return {.retryAvailable = true};
}

} // namespace ztermy::ai
