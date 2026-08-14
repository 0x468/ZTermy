#pragma once

#include "domain/ai/AiContextRedactor.h"
#include "domain/terminal/CommandBlockStore.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ztermy::ai
{

enum class AiContextItemKind : std::uint8_t
{
    explicitAttachment,
    commandBlock,
    currentTerminalFrame,
};

struct AiContextLimits final
{
    std::size_t maxTotalBytes = std::size_t{64} * 1024;
    std::size_t maxTotalLines = 1000;
    std::size_t maxEstimatedTokens = 16'000;
    std::size_t maxItemBytes = std::size_t{16} * 1024;
    std::size_t maxItemLines = 300;
    std::size_t maxPrecedingBlocks = 5;
    std::size_t maxRedactionWindowBytes = std::size_t{64} * 1024;
};

struct AiExplicitContext final
{
    std::string id;
    std::string title;
    std::string content;
    std::string source;
    bool truncated = false;
};

struct AiTerminalFrameContext final
{
    std::string id;
    std::string content;
    std::string sessionId;
    std::string host;
    std::string shell;
    std::string workingDirectory;
    std::uint64_t sessionGeneration = 0;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
};

struct AiContextRequest final
{
    std::optional<terminal::CommandBlockId> primaryBlockId;
    bool preferLastFailure = false;
    bool automaticContextEnabled = true;
    std::vector<AiExplicitContext> explicitItems;
    std::optional<AiTerminalFrameContext> currentFrame;
    std::unordered_set<std::string> excludedItemIds;
    std::unordered_set<std::string> pinnedItemIds;
    std::vector<AiUserRedactionRule> redactionRules;
};

struct AiContextItem final
{
    std::string id;
    AiContextItemKind kind = AiContextItemKind::commandBlock;
    std::string title;
    std::string content;
    std::string command;
    std::string workingDirectory;
    std::string sessionId;
    std::string host;
    std::string shell;
    std::uint64_t sessionGeneration = 0;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
    terminal::CommandBoundaryConfidence boundaryConfidence = terminal::CommandBoundaryConfidence::unknown;
    terminal::CommandOutputCoverage outputCoverage = terminal::CommandOutputCoverage::unknown;
    std::optional<int> exitStatus;
    std::size_t accountedBytes = 0;
    std::size_t lineCount = 0;
    std::size_t estimatedTokens = 0;
    bool pinned = false;
    bool automatic = false;
    bool truncated = false;
    bool untrustedEvidence = true;
    std::size_t redactionCount = 0;
    bool redacted = false;
};

struct AiContextBundle final
{
    std::vector<AiContextItem> items;
    std::size_t totalBytes = 0;
    std::size_t totalLines = 0;
    std::size_t estimatedTokens = 0;
    std::size_t droppedItems = 0;
    bool aggregateTruncated = false;
    std::size_t totalRedactions = 0;
    std::vector<std::string> invalidRedactionRuleIds;
};

class AiContextBroker final
{
public:
    explicit AiContextBroker(AiContextLimits limits = {});

    [[nodiscard]] static std::string normalizeTerminalText(std::string_view text);

    [[nodiscard]] const AiContextLimits &limits() const noexcept;
    [[nodiscard]] AiContextBundle build(const terminal::CommandBlockStore &store,
                                        const AiContextRequest &request) const;
    [[nodiscard]] AiContextBundle build(std::span<const terminal::CommandBlock> blocks,
                                        const AiContextRequest &request) const;

private:
    AiContextLimits m_limits;
};

} // namespace ztermy::ai
