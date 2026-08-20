#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

// Typed context compaction for provider requests, modeled on the Netcatty
// typed-compression stage and the preserve-recent-turns budget
// (docs/reviews/ai-product-design-research-2026-08.md section 8). It is a
// deterministic, provider-independent byte/character pass: old messages are
// head/tail truncated while the most recent messages stay verbatim, so a long
// conversation cannot blow past the model context window. The conversation
// model itself is never mutated; the compaction applies to the request view.
struct AiCompactionLimits final
{
    std::size_t contextWindowTokens = 128'000;
    std::size_t reservedOutputTokens = 16'000;
    std::size_t reserveBufferTokens = 8'000;
    std::size_t preserveRecentMessages = 10;
    std::size_t oldMessageHeadCharacters = 800;
    std::size_t oldMessageTailCharacters = 4'000;
    std::size_t maximumToolOutputCharacters = 2'000;
};

struct AiCompactionResult final
{
    AiGenerationRequest request;
    std::size_t estimatedInputTokens = 0;
    std::size_t compactedMessageCount = 0;
    std::size_t compactedCharacters = 0;
    bool compacted = false;
    bool overBudget = false;
};

class AiContextCompactor final
{
public:
    [[nodiscard]] static std::size_t estimateTokens(std::string_view text);

    [[nodiscard]] static std::size_t estimateRequestTokens(const AiGenerationRequest &request);

    [[nodiscard]] static AiCompactionResult compact(AiGenerationRequest request, const AiCompactionLimits &limits = {});
};

} // namespace ztermy::ai
