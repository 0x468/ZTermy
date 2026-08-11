#pragma once

#include "domain/terminal/CommandBlockStore.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AiReadToolErrorCode : std::uint8_t
{
    invalidArguments,
    sessionNotFound,
    staleSessionGeneration,
    commandBlockNotFound,
    rangeOutOfBounds,
    limitExceeded,
};

struct AiReadToolError final
{
    AiReadToolErrorCode code = AiReadToolErrorCode::invalidArguments;
    std::string message;
};

struct AiReadToolLimits final
{
    std::size_t maxSessions = 64;
    std::size_t maxTerminalLines = 200;
    std::size_t maxTerminalBytes = std::size_t{32} * 1024;
    std::size_t maxCommandOutputBytes = std::size_t{16} * 1024;
};

struct AiTerminalReadSnapshot final
{
    std::string sessionId;
    std::string title;
    std::string host;
    std::string shell;
    std::string workingDirectory;
    std::string terminalFrame;
    std::uint64_t sessionGeneration = 0;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
    bool connected = false;
    std::vector<terminal::CommandBlock> commandBlocks;
};

struct AiSessionSummary final
{
    std::string sessionId;
    std::string title;
    std::string host;
    std::string shell;
    std::string workingDirectory;
    std::uint64_t sessionGeneration = 0;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
    bool connected = false;
    std::size_t commandBlockCount = 0;
};

struct AiTerminalRange final
{
    std::string sessionId;
    std::uint64_t sessionGeneration = 0;
    std::string content;
    std::size_t firstLine = 0;
    std::size_t lineCount = 0;
    std::size_t totalLines = 0;
    std::size_t nextLine = 0;
    bool hasMore = false;
    bool truncated = false;
    bool untrustedEvidence = true;
};

struct AiCommandBlockRead final
{
    terminal::CommandBlockId id = 0;
    std::string sessionId;
    std::uint64_t sessionGeneration = 0;
    std::string command;
    std::string workingDirectory;
    std::string host;
    std::string shell;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
    terminal::CommandBoundaryConfidence boundaryConfidence = terminal::CommandBoundaryConfidence::unknown;
    terminal::CommandOutputCoverage outputCoverage = terminal::CommandOutputCoverage::unknown;
    terminal::CommandBlockState state = terminal::CommandBlockState::running;
    std::optional<int> exitStatus;
    std::string output;
    std::uint64_t observedOutputBytes = 0;
    std::uint64_t omittedOutputBytes = 0;
    std::uint64_t missingOutputBytes = 0;
    bool truncated = false;
    bool hasInterleavedOutput = false;
    bool untrustedEvidence = true;
};

class AiReadTools final
{
public:
    explicit AiReadTools(AiReadToolLimits limits = {});

    [[nodiscard]] const AiReadToolLimits &limits() const noexcept;
    [[nodiscard]] std::expected<std::vector<AiSessionSummary>, AiReadToolError>
    listSessions(std::span<const AiTerminalReadSnapshot> sessions) const;
    [[nodiscard]] std::expected<AiSessionSummary, AiReadToolError>
    readSessionInfo(std::span<const AiTerminalReadSnapshot> sessions,
                    std::string_view sessionId,
                    std::uint64_t sessionGeneration) const;
    [[nodiscard]] std::expected<AiTerminalRange, AiReadToolError>
    readTerminal(std::span<const AiTerminalReadSnapshot> sessions,
                 std::string_view sessionId,
                 std::uint64_t sessionGeneration,
                 std::size_t firstLine,
                 std::size_t lineCount) const;
    [[nodiscard]] std::expected<AiCommandBlockRead, AiReadToolError>
    readCommandBlock(std::span<const AiTerminalReadSnapshot> sessions,
                     std::string_view sessionId,
                     std::uint64_t sessionGeneration,
                     terminal::CommandBlockId blockId) const;

private:
    [[nodiscard]] std::expected<const AiTerminalReadSnapshot *, AiReadToolError>
    findSession(std::span<const AiTerminalReadSnapshot> sessions,
                std::string_view sessionId,
                std::uint64_t sessionGeneration) const;

    AiReadToolLimits m_limits;
};

} // namespace ztermy::ai
