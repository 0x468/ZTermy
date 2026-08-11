#include "domain/ai/AiToolDispatchLedger.h"

#include <algorithm>
#include <utility>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] bool sameDispatch(const AiToolDispatchRequest &left, const AiToolDispatchRequest &right) noexcept
{
    return left.toolName == right.toolName && left.canonicalArguments == right.canonicalArguments
           && left.sessionId == right.sessionId && left.sessionGeneration == right.sessionGeneration
           && left.sideEffecting == right.sideEffecting;
}

} // namespace

AiToolDispatchLedger::AiToolDispatchLedger(const AiToolDispatchLimits limits) : m_limits(limits) {}

AiToolDispatchOutcome AiToolDispatchLedger::begin(AiToolDispatchRequest request)
{
    std::scoped_lock lock(m_mutex);
    if (!valid(request))
    {
        return {.admission = AiToolDispatchAdmission::invalidRequest};
    }
    const auto existing = std::ranges::find_if(m_records, [&request](const AiToolDispatchRecord &record) {
        return record.request.key == request.key;
    });
    if (existing != m_records.end())
    {
        if (!sameDispatch(existing->request, request))
        {
            return {.admission = AiToolDispatchAdmission::duplicateMismatch, .record = *existing};
        }
        return {.admission =
                    terminal(existing->state) ? AiToolDispatchAdmission::cached : AiToolDispatchAdmission::joined,
                .record = *existing};
    }
    if (!makeRoom())
    {
        return {.admission = AiToolDispatchAdmission::capacityExceeded};
    }
    m_records.push_back(AiToolDispatchRecord{.request = std::move(request), .sequence = m_nextSequence++});
    return {.admission = AiToolDispatchAdmission::accepted, .record = m_records.back()};
}

bool AiToolDispatchLedger::transition(const AiToolDispatchKey &key, const AiToolDispatchState state,
                                      std::string resultJson)
{
    std::scoped_lock lock(m_mutex);
    const auto record = std::ranges::find_if(m_records, [&key](const AiToolDispatchRecord &value) {
        return value.request.key == key;
    });
    if (record == m_records.end() || !transitionAllowed(record->state, state)
        || resultJson.size() > m_limits.maximumResultBytes || (!terminal(state) && !resultJson.empty()))
    {
        return false;
    }
    record->state = state;
    record->resultJson = std::move(resultJson);
    return true;
}

std::optional<AiToolDispatchRecord> AiToolDispatchLedger::find(const AiToolDispatchKey &key) const
{
    std::scoped_lock lock(m_mutex);
    const auto record = std::ranges::find_if(m_records, [&key](const AiToolDispatchRecord &value) {
        return value.request.key == key;
    });
    return record == m_records.end() ? std::nullopt : std::optional<AiToolDispatchRecord>{*record};
}

std::size_t AiToolDispatchLedger::size() const
{
    std::scoped_lock lock(m_mutex);
    return m_records.size();
}

void AiToolDispatchLedger::clearConversation(const std::string_view conversationId)
{
    std::scoped_lock lock(m_mutex);
    std::erase_if(m_records, [conversationId](const AiToolDispatchRecord &record) {
        return record.request.key.conversationId == conversationId;
    });
}

bool AiToolDispatchLedger::valid(const AiToolDispatchRequest &request) const noexcept
{
    const auto validIdentifier = [this](const std::string &value) {
        return !value.empty() && value.size() <= m_limits.maximumIdentifierBytes && !value.contains('\0');
    };
    return m_limits.maximumRecords > 0 && validIdentifier(request.key.conversationId) && request.key.turnId > 0
           && validIdentifier(request.key.toolCallId) && validIdentifier(request.toolName)
           && validIdentifier(request.sessionId) && request.canonicalArguments.size() <= m_limits.maximumArgumentsBytes
           && !request.canonicalArguments.contains('\0');
}

bool AiToolDispatchLedger::terminal(const AiToolDispatchState state) noexcept
{
    return state == AiToolDispatchState::succeeded || state == AiToolDispatchState::failed
           || state == AiToolDispatchState::cancelled || state == AiToolDispatchState::outcomeUnknown;
}

bool AiToolDispatchLedger::transitionAllowed(const AiToolDispatchState from, const AiToolDispatchState to) noexcept
{
    if (terminal(from) || from == to)
    {
        return false;
    }
    if (from == AiToolDispatchState::pending)
    {
        return to == AiToolDispatchState::awaitingApproval || to == AiToolDispatchState::running || terminal(to);
    }
    if (from == AiToolDispatchState::awaitingApproval)
    {
        return to == AiToolDispatchState::running || to == AiToolDispatchState::failed
               || to == AiToolDispatchState::cancelled;
    }
    return from == AiToolDispatchState::running && terminal(to);
}

bool AiToolDispatchLedger::makeRoom()
{
    if (m_records.size() < m_limits.maximumRecords)
    {
        return true;
    }
    const auto oldestTerminal = std::ranges::find_if(m_records, [](const AiToolDispatchRecord &record) {
        return terminal(record.state);
    });
    if (oldestTerminal == m_records.end())
    {
        return false;
    }
    m_records.erase(oldestTerminal);
    return true;
}

} // namespace ztermy::ai
