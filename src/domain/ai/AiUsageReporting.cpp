#include "domain/ai/AiUsageReporting.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace ztermy::ai
{
namespace
{

// Standard text-token rates reviewed against the official OpenAI model pages on 2026-08-12.
// Estimates deliberately do not cover Batch, Flex, Priority, regional, cache-write, image, or tool-call charges.
// Unknown models and OpenAI-compatible third-party endpoints therefore return no monetary estimate.
constexpr std::string_view kCatalogDate = "2026-08-12";
constexpr std::uint64_t kLongContextThreshold = 272'000;

struct ModelRate final
{
    std::string_view prefix;
    double inputPerMillionUsd = 0.0;
    double cachedInputPerMillionUsd = 0.0;
    double outputPerMillionUsd = 0.0;
    bool longContextPremium = false;
};

constexpr std::array kRates{
    ModelRate{.prefix = "gpt-5.6-terra",
              .inputPerMillionUsd = 2.50,
              .cachedInputPerMillionUsd = 0.25,
              .outputPerMillionUsd = 15.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.6-luna",
              .inputPerMillionUsd = 1.00,
              .cachedInputPerMillionUsd = 0.10,
              .outputPerMillionUsd = 6.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.6-sol",
              .inputPerMillionUsd = 5.00,
              .cachedInputPerMillionUsd = 0.50,
              .outputPerMillionUsd = 30.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.6",
              .inputPerMillionUsd = 5.00,
              .cachedInputPerMillionUsd = 0.50,
              .outputPerMillionUsd = 30.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.4-pro",
              .inputPerMillionUsd = 30.00,
              .cachedInputPerMillionUsd = -1.0,
              .outputPerMillionUsd = 180.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.4",
              .inputPerMillionUsd = 2.50,
              .cachedInputPerMillionUsd = 0.25,
              .outputPerMillionUsd = 15.00,
              .longContextPremium = true},
    ModelRate{.prefix = "gpt-5.2-pro",
              .inputPerMillionUsd = 21.00,
              .cachedInputPerMillionUsd = -1.0,
              .outputPerMillionUsd = 168.00,
              .longContextPremium = false},
    ModelRate{.prefix = "gpt-5.2",
              .inputPerMillionUsd = 1.75,
              .cachedInputPerMillionUsd = 0.175,
              .outputPerMillionUsd = 14.00,
              .longContextPremium = false},
    ModelRate{.prefix = "gpt-5.1",
              .inputPerMillionUsd = 1.25,
              .cachedInputPerMillionUsd = 0.125,
              .outputPerMillionUsd = 10.00,
              .longContextPremium = false},
    ModelRate{.prefix = "gpt-5-pro",
              .inputPerMillionUsd = 15.00,
              .cachedInputPerMillionUsd = -1.0,
              .outputPerMillionUsd = 120.00,
              .longContextPremium = false},
    ModelRate{.prefix = "gpt-5",
              .inputPerMillionUsd = 1.25,
              .cachedInputPerMillionUsd = 0.125,
              .outputPerMillionUsd = 10.00,
              .longContextPremium = false},
};

[[nodiscard]] std::string normalizedModel(std::string_view model)
{
    std::string normalized(model);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized;
}

[[nodiscard]] const ModelRate *findRate(const std::string_view model) noexcept
{
    const auto match = std::ranges::find_if(kRates, [model](const ModelRate &rate) {
        if (model == rate.prefix || !model.starts_with(rate.prefix))
        {
            return model == rate.prefix;
        }
        const auto suffix = model.substr(rate.prefix.size());
        return suffix.starts_with("-20") || suffix == "-chat-latest";
    });
    return match == kRates.end() ? nullptr : &*match;
}

} // namespace

AiCostEstimate AiUsageEstimator::estimate(const AiProviderKind provider, const std::string_view model,
                                          const AiTokenUsage &usage, const bool officialOpenAiEndpoint)
{
    if (provider != AiProviderKind::openAiResponses || !officialOpenAiEndpoint)
    {
        return {};
    }

    const auto normalized = normalizedModel(model);
    const auto *rate = findRate(normalized);
    if (rate == nullptr)
    {
        return {};
    }

    const auto cachedTokens = std::min(usage.cachedInputTokens, usage.inputTokens);
    const auto uncachedTokens = usage.inputTokens - cachedTokens;
    const bool longContext = rate->longContextPremium && usage.inputTokens > kLongContextThreshold;
    const double inputMultiplier = longContext ? 2.0 : 1.0;
    const double outputMultiplier = longContext ? 1.5 : 1.0;
    const double inputCost = static_cast<double>(uncachedTokens) * rate->inputPerMillionUsd * inputMultiplier;
    const double cachedRate =
        rate->cachedInputPerMillionUsd >= 0.0 ? rate->cachedInputPerMillionUsd : rate->inputPerMillionUsd;
    const double cachedCost = static_cast<double>(cachedTokens) * cachedRate * inputMultiplier;
    const double outputCost = static_cast<double>(usage.outputTokens) * rate->outputPerMillionUsd * outputMultiplier;

    return AiCostEstimate{.usd = (inputCost + cachedCost + outputCost) / 1'000'000.0,
                          .catalogDate = kCatalogDate,
                          .longContextRatesApplied = longContext};
}

} // namespace ztermy::ai
