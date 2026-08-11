#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ztermy::ai
{

struct AiTurnMetrics final
{
    std::uint64_t wallTimeMilliseconds = 0;
    std::optional<std::uint64_t> firstTokenMilliseconds;
    std::uint32_t retryCount = 0;
};

struct AiCostEstimate final
{
    std::optional<double> usd;
    std::string_view catalogDate;
    bool longContextRatesApplied = false;
};

class AiUsageEstimator final
{
public:
    [[nodiscard]] static AiCostEstimate estimate(AiProviderKind provider, std::string_view model,
                                                 const AiTokenUsage &usage, bool officialOpenAiEndpoint);
};

} // namespace ztermy::ai
