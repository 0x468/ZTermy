#include "domain/ai/AiToolEvidence.h"

namespace ztermy::ai
{

AiToolEvidenceVerdict evaluateToolEvidence(const std::span<const AiToolActivity> activities) noexcept
{
    AiToolEvidenceVerdict verdict;
    for (const auto &activity : activities)
    {
        if (activity.state == "succeeded")
        {
            ++verdict.succeededCount;
            continue;
        }
        if (activity.state == "failed" || activity.state == "cancelled")
        {
            ++verdict.failedCount;
            if (activity.sideEffecting)
            {
                ++verdict.failedSideEffectCount;
            }
            continue;
        }
        ++verdict.pendingCount;
    }

    if (activities.empty())
    {
        verdict.state = AiToolEvidenceState::none;
    }
    else if (verdict.failedCount > 0)
    {
        verdict.state = AiToolEvidenceState::incomplete;
    }
    else if (verdict.pendingCount > 0)
    {
        verdict.state = AiToolEvidenceState::pending;
    }
    else
    {
        verdict.state = AiToolEvidenceState::verified;
    }
    return verdict;
}

std::string_view aiToolEvidenceStateToken(const AiToolEvidenceState state) noexcept
{
    switch (state)
    {
        case AiToolEvidenceState::none:
            return "none";
        case AiToolEvidenceState::verified:
            return "verified";
        case AiToolEvidenceState::pending:
            return "pending";
        case AiToolEvidenceState::incomplete:
            return "incomplete";
    }
    return "none";
}

} // namespace ztermy::ai
