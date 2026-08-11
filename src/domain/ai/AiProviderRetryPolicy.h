#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstdint>

namespace ztermy::ai
{

struct AiProviderRetryLimits final
{
    std::uint32_t maxRetries = 2;
    std::uint64_t baseDelayMilliseconds = 500;
    std::uint64_t maxDelayMilliseconds = 8'000;
    std::uint32_t jitterPercent = 20;
};

struct AiProviderRetryDecision final
{
    bool retry = false;
    std::uint64_t delayMilliseconds = 0;
};

class AiProviderRetryPolicy final
{
public:
    explicit AiProviderRetryPolicy(AiProviderRetryLimits limits = {});

    [[nodiscard]] const AiProviderRetryLimits &limits() const noexcept;
    [[nodiscard]] AiProviderRetryDecision decide(const AiProviderError &error, std::uint32_t completedRetries,
                                                 double jitterSample = 0.5) const noexcept;

private:
    AiProviderRetryLimits m_limits;
};

} // namespace ztermy::ai
