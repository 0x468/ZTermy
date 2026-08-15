#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace ztermy::ai
{

enum class AiToolEvidenceState : std::uint8_t
{
    none,
    verified,
    pending,
    incomplete,
};

struct AiToolEvidenceVerdict final
{
    AiToolEvidenceState state = AiToolEvidenceState::none;
    std::uint32_t succeededCount = 0;
    std::uint32_t pendingCount = 0;
    std::uint32_t failedCount = 0;
    std::uint32_t failedSideEffectCount = 0;

    [[nodiscard]] friend bool operator==(const AiToolEvidenceVerdict &, const AiToolEvidenceVerdict &) = default;
};

[[nodiscard]] AiToolEvidenceVerdict evaluateToolEvidence(std::span<const AiToolActivity> activities) noexcept;
[[nodiscard]] std::string_view aiToolEvidenceStateToken(AiToolEvidenceState state) noexcept;

} // namespace ztermy::ai
