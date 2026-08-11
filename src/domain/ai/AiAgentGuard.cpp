#include "domain/ai/AiAgentGuard.h"

#include <algorithm>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] bool validIdentifier(const std::string_view value) noexcept
{
    constexpr std::size_t maximumIdentifierBytes = 256;
    return !value.empty() && value.size() <= maximumIdentifierBytes && !value.contains('\0');
}

} // namespace

AiSessionWriteOwnership::AiSessionWriteOwnership(const std::size_t maximumSessions) : m_maximumSessions(maximumSessions)
{
}

AiWriteOwnershipResult AiSessionWriteOwnership::claim(const AiSessionTarget &target,
                                                      const std::string_view conversationId)
{
    if (!validIdentifier(target.sessionId) || !validIdentifier(conversationId))
    {
        return AiWriteOwnershipResult::invalid;
    }
    std::scoped_lock lock(m_mutex);
    const auto existing = std::ranges::find(m_owners, target, &AiWriteOwnership::target);
    if (existing != m_owners.end())
    {
        return existing->conversationId == conversationId ? AiWriteOwnershipResult::alreadyOwned
                                                          : AiWriteOwnershipResult::conflict;
    }
    if (m_owners.size() >= m_maximumSessions)
    {
        return AiWriteOwnershipResult::capacityExceeded;
    }
    m_owners.push_back(AiWriteOwnership{.target = target, .conversationId = std::string(conversationId)});
    return AiWriteOwnershipResult::acquired;
}

bool AiSessionWriteOwnership::transfer(const AiSessionTarget &target, const std::string_view currentOwner,
                                       const std::string_view nextOwner)
{
    if (!validIdentifier(currentOwner) || !validIdentifier(nextOwner))
    {
        return false;
    }
    std::scoped_lock lock(m_mutex);
    const auto existing = std::ranges::find(m_owners, target, &AiWriteOwnership::target);
    if (existing == m_owners.end() || existing->conversationId != currentOwner)
    {
        return false;
    }
    existing->conversationId = nextOwner;
    return true;
}

std::optional<std::string> AiSessionWriteOwnership::owner(const AiSessionTarget &target) const
{
    std::scoped_lock lock(m_mutex);
    const auto existing = std::ranges::find(m_owners, target, &AiWriteOwnership::target);
    return existing == m_owners.end() ? std::nullopt : std::optional<std::string>{existing->conversationId};
}

void AiSessionWriteOwnership::releaseConversation(const std::string_view conversationId)
{
    std::scoped_lock lock(m_mutex);
    std::erase_if(m_owners, [conversationId](const AiWriteOwnership &ownerValue) {
        return ownerValue.conversationId == conversationId;
    });
}

void AiSessionWriteOwnership::releaseSession(const AiSessionTarget &target)
{
    std::scoped_lock lock(m_mutex);
    std::erase_if(m_owners, [&target](const AiWriteOwnership &ownerValue) {
        return ownerValue.target == target;
    });
}

AiAgentTurnBudget::AiAgentTurnBudget(const AiAgentTurnLimits limits, const Clock::time_point startedAt)
    : m_limits(limits), m_startedAt(startedAt)
{
}

AiAgentBudgetDecision AiAgentTurnBudget::authorize(const bool write, const std::string_view readSignature,
                                                   const std::uint64_t stateGeneration, const Clock::time_point now)
{
    if (now < m_startedAt || now - m_startedAt > m_limits.maximumDuration)
    {
        return AiAgentBudgetDecision::timeLimit;
    }
    if (m_toolCalls >= m_limits.maximumToolCalls)
    {
        return AiAgentBudgetDecision::toolCallLimit;
    }
    if (write && m_writeActions >= m_limits.maximumWriteActions)
    {
        return AiAgentBudgetDecision::writeActionLimit;
    }
    if (!write && !readSignature.empty())
    {
        if (m_limits.maximumRepeatedReads == 0)
        {
            return AiAgentBudgetDecision::repeatedReadLimit;
        }
        auto observation = std::ranges::find(m_readObservations, readSignature, &ReadObservation::signature);
        if (observation == m_readObservations.end())
        {
            m_readObservations.push_back(ReadObservation{.signature = std::string(readSignature),
                                                         .stateGeneration = stateGeneration,
                                                         .count = 1});
        }
        else if (observation->stateGeneration != stateGeneration)
        {
            observation->stateGeneration = stateGeneration;
            observation->count = 1;
        }
        else if (observation->count >= m_limits.maximumRepeatedReads)
        {
            return AiAgentBudgetDecision::repeatedReadLimit;
        }
        else
        {
            ++observation->count;
        }
    }
    ++m_toolCalls;
    if (write)
    {
        ++m_writeActions;
    }
    return AiAgentBudgetDecision::allow;
}

AiAgentBudgetDecision AiAgentTurnBudget::observeTokenUsage(const std::uint64_t totalTokens) noexcept
{
    return totalTokens > m_limits.maximumTokenUsage ? AiAgentBudgetDecision::tokenLimit : AiAgentBudgetDecision::allow;
}

std::uint32_t AiAgentTurnBudget::toolCalls() const noexcept
{
    return m_toolCalls;
}

std::uint32_t AiAgentTurnBudget::writeActions() const noexcept
{
    return m_writeActions;
}

} // namespace ztermy::ai
