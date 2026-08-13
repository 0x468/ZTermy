#pragma once

#include <QString>

namespace ztermy::ai
{

// Layered system prompt for the ztermy terminal agent. The structure follows
// the opencode/Netcatty reference research (docs/reviews/ai-product-design-
// research-2026-08.md): identity, evidence boundary, reading strategy, command
// protocol, tool policy, and output format rules. The builder is a pure
// function so the prompt can be locked by unit tests.
class AiSystemPromptBuilder final
{
public:
    [[nodiscard]] static QString build(bool commandRequest);
};

} // namespace ztermy::ai
