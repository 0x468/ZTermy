#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct AiSessionTarget final
{
    std::string sessionId;
    std::uint64_t sessionGeneration = 0;

    [[nodiscard]] bool operator==(const AiSessionTarget &) const noexcept = default;
};

enum class AiWriteOwnershipResult : std::uint8_t
{
    acquired,
    alreadyOwned,
    conflict,
    invalid,
    capacityExceeded,
};

struct AiWriteOwnership final
{
    AiSessionTarget target;
    std::string conversationId;
};

class AiSessionWriteOwnership final
{
public:
    explicit AiSessionWriteOwnership(std::size_t maximumSessions = 64);

    [[nodiscard]] AiWriteOwnershipResult claim(const AiSessionTarget &target, std::string_view conversationId);
    [[nodiscard]] bool transfer(const AiSessionTarget &target, std::string_view currentOwner,
                                std::string_view nextOwner);
    [[nodiscard]] std::optional<std::string> owner(const AiSessionTarget &target) const;
    void releaseConversation(std::string_view conversationId);
    void releaseSession(const AiSessionTarget &target);

private:
    std::size_t m_maximumSessions;
    mutable std::mutex m_mutex;
    std::vector<AiWriteOwnership> m_owners;
};

struct AiAgentTurnLimits final
{
    std::uint32_t maximumToolCalls = 24;
    std::uint32_t maximumWriteActions = 8;
    std::uint32_t maximumRepeatedReads = 3;
    std::uint64_t maximumTokenUsage = 64'000;
    std::chrono::milliseconds maximumDuration = std::chrono::minutes(15);
};

enum class AiAgentBudgetDecision : std::uint8_t
{
    allow,
    toolCallLimit,
    writeActionLimit,
    repeatedReadLimit,
    tokenLimit,
    timeLimit,
};

class AiAgentTurnBudget final
{
public:
    using Clock = std::chrono::steady_clock;

    explicit AiAgentTurnBudget(AiAgentTurnLimits limits = {}, Clock::time_point startedAt = Clock::now());

    [[nodiscard]] AiAgentBudgetDecision authorize(bool write, std::string_view readSignature = {},
                                                  std::uint64_t stateGeneration = 0,
                                                  Clock::time_point now = Clock::now());
    [[nodiscard]] AiAgentBudgetDecision observeTokenUsage(std::uint64_t totalTokens) noexcept;
    [[nodiscard]] std::uint32_t toolCalls() const noexcept;
    [[nodiscard]] std::uint32_t writeActions() const noexcept;

private:
    struct ReadObservation final
    {
        std::string signature;
        std::uint64_t stateGeneration = 0;
        std::uint32_t count = 0;
    };

    AiAgentTurnLimits m_limits;
    Clock::time_point m_startedAt;
    std::vector<ReadObservation> m_readObservations;
    std::uint32_t m_toolCalls = 0;
    std::uint32_t m_writeActions = 0;
};

} // namespace ztermy::ai
