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
    cursorExpired,
    limitExceeded,
};

struct AiReadToolError final
{
    AiReadToolErrorCode code = AiReadToolErrorCode::invalidArguments;
    std::string message;
    std::optional<std::uint64_t> nextAvailableCursor;
};

struct AiReadToolLimits final
{
    std::size_t maxSessions = 64;
    std::size_t maxTerminalLines = 200;
    std::size_t maxTerminalBytes = std::size_t{32} * 1024;
    std::size_t maxCommandOutputBytes = std::size_t{16} * 1024;
    std::size_t maxOperationItems = 100;
};

struct AiSftpEntrySnapshot final
{
    std::string name;
    std::string remotePath;
    std::string type;
    std::uint64_t size = 0;
    std::optional<std::int64_t> modifiedUtcSeconds;
    std::string permissions;
    bool hidden = false;
};

struct AiShellHistorySnapshot final
{
    std::string command;
    std::string shell;
    std::optional<std::int64_t> timestampUtcSeconds;
};

struct AiScriptSnapshot final
{
    struct Variable final
    {
        std::string name;
        std::string label;
        std::string type;
        std::vector<std::string> choices;
        bool required = false;
    };

    struct Step final
    {
        std::string command;
        std::string continuation;
        std::string outputMarker;
        std::uint32_t timeoutMs = 0;
    };

    std::string id;
    std::string name;
    std::string description;
    std::string shell;
    std::size_t variableCount = 0;
    std::size_t stepCount = 0;
    std::int64_t modifiedUtcMs = 0;
    std::vector<Variable> variables;
    std::vector<Step> steps;
};

struct AiNoteSnapshot final
{
    std::string path;
    std::string name;
    std::uint64_t size = 0;
    std::int64_t modifiedUtcMs = 0;
    bool folder = false;
};

struct AiPortForwardingSnapshot final
{
    std::string id;
    std::string label;
    std::string profileName;
    std::string type;
    std::string bindHost;
    std::uint16_t bindPort = 0;
    std::string destinationHost;
    std::uint16_t destinationPort = 0;
    std::string state;
    std::string failure;
    std::uint64_t activeClients = 0;
    std::uint64_t bytesFromClients = 0;
    std::uint64_t bytesToClients = 0;
    std::uint64_t rejectedClients = 0;
};

struct AiTelemetrySnapshot final
{
    std::string state;
    std::string osName;
    std::optional<double> cpuPercent;
    std::uint32_t cpuCoreCount = 0;
    std::uint64_t memoryUsedKiB = 0;
    std::uint64_t memoryTotalKiB = 0;
    std::uint64_t receivedBytesPerSecond = 0;
    std::uint64_t transmittedBytesPerSecond = 0;
    std::uint32_t sshProbeLatencyMs = 0;
};

struct AiOperationsReadSnapshot final
{
    std::string sftpState;
    std::string sftpPath;
    std::string sftpHomePath;
    bool sftpListingAvailable = false;
    std::vector<AiSftpEntrySnapshot> sftpEntries;
    std::vector<AiShellHistorySnapshot> shellHistory;
    std::vector<AiScriptSnapshot> scripts;
    std::vector<AiNoteSnapshot> notes;
    std::vector<AiPortForwardingSnapshot> portForwarding;
    AiTelemetrySnapshot telemetry;
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
    AiOperationsReadSnapshot operations;
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

struct AiCommandOutputRead final
{
    terminal::CommandBlockId id = 0;
    std::string sessionId;
    std::uint64_t sessionGeneration = 0;
    terminal::CommandBlockState state = terminal::CommandBlockState::running;
    terminal::CommandOutputCoverage outputCoverage = terminal::CommandOutputCoverage::unknown;
    std::optional<int> exitStatus;
    std::string output;
    std::uint64_t requestedCursor = 0;
    std::uint64_t nextCursor = 0;
    std::uint64_t streamStart = 0;
    std::uint64_t streamEnd = 0;
    std::uint64_t skippedBytes = 0;
    bool hasMore = false;
    bool truncated = false;
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
    readSessionInfo(std::span<const AiTerminalReadSnapshot> sessions, std::string_view sessionId,
                    std::uint64_t sessionGeneration) const;
    [[nodiscard]] std::expected<AiTerminalRange, AiReadToolError>
    readTerminal(std::span<const AiTerminalReadSnapshot> sessions, std::string_view sessionId,
                 std::uint64_t sessionGeneration, std::size_t firstLine, std::size_t lineCount) const;
    [[nodiscard]] std::expected<AiCommandBlockRead, AiReadToolError>
    readCommandBlock(std::span<const AiTerminalReadSnapshot> sessions, std::string_view sessionId,
                     std::uint64_t sessionGeneration, terminal::CommandBlockId blockId) const;
    [[nodiscard]] std::expected<AiCommandOutputRead, AiReadToolError>
    readCommandOutput(std::span<const AiTerminalReadSnapshot> sessions, std::string_view sessionId,
                      std::uint64_t sessionGeneration, terminal::CommandBlockId blockId, std::uint64_t afterCursor,
                      std::size_t maximumBytes) const;

private:
    [[nodiscard]] std::expected<const AiTerminalReadSnapshot *, AiReadToolError>
    findSession(std::span<const AiTerminalReadSnapshot> sessions, std::string_view sessionId,
                std::uint64_t sessionGeneration) const;

    AiReadToolLimits m_limits;
};

} // namespace ztermy::ai
