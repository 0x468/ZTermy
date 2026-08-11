#include "domain/ai/AiCommandTracker.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace ztermy::ai
{

AiCommandTracker::AiCommandTracker(const std::size_t maximumCommands)
    : m_maximumCommands(std::max(maximumCommands, std::size_t{1}))
{
    m_commands.reserve(m_maximumCommands);
}

bool AiCommandTracker::accept(AiTrackedCommand command)
{
    if (command.id.empty() || command.conversationId.empty() || command.target.sessionId.empty()
        || command.command.empty() || std::ranges::any_of(m_commands, [&command](const AiTrackedCommand &value) {
               return value.id == command.id;
           }))
    {
        return false;
    }
    if (m_commands.size() >= m_maximumCommands && !makeRoom())
    {
        return false;
    }
    m_commands.push_back(std::move(command));
    return true;
}

std::optional<AiTrackedCommand> AiCommandTracker::observe(const std::string_view commandId,
                                                          const std::span<const terminal::CommandBlock> blocks,
                                                          const bool connected)
{
    const auto position = std::ranges::find(m_commands, commandId, &AiTrackedCommand::id);
    if (position == m_commands.end())
    {
        return std::nullopt;
    }
    if (!position->blockId.has_value())
    {
        const auto block = std::ranges::find_if(blocks, [&command = *position](const terminal::CommandBlock &value) {
            return value.id > command.baselineBlockId && value.sessionId == command.target.sessionId
                   && value.sessionGeneration == command.target.sessionGeneration && value.command == command.command;
        });
        if (block != blocks.end())
        {
            position->blockId = block->id;
        }
    }
    if (!position->blockId.has_value())
    {
        position->state = connected ? AiTrackedCommandState::queued : AiTrackedCommandState::disconnected;
        return *position;
    }
    const auto block = std::ranges::find(blocks, *position->blockId, &terminal::CommandBlock::id);
    if (block == blocks.end())
    {
        position->state = connected ? AiTrackedCommandState::outcomeUnknown : AiTrackedCommandState::disconnected;
        return *position;
    }
    if (block->state == terminal::CommandBlockState::running)
    {
        position->state = AiTrackedCommandState::running;
        position->exitStatus.reset();
        return *position;
    }
    position->state = block->completionReason == terminal::CommandCompletionReason::disconnect
                          ? AiTrackedCommandState::disconnected
                          : AiTrackedCommandState::finished;
    position->exitStatus = block->exitStatus;
    return *position;
}

std::optional<AiTrackedCommand> AiCommandTracker::find(const std::string_view commandId) const
{
    const auto position = std::ranges::find(m_commands, commandId, &AiTrackedCommand::id);
    return position == m_commands.end() ? std::nullopt : std::optional<AiTrackedCommand>{*position};
}

bool AiCommandTracker::markInterruptRequested(const std::string_view commandId)
{
    const auto position = std::ranges::find(m_commands, commandId, &AiTrackedCommand::id);
    if (position == m_commands.end() || position->state == AiTrackedCommandState::finished
        || position->state == AiTrackedCommandState::disconnected)
    {
        return false;
    }
    position->interruptRequested = true;
    return true;
}

void AiCommandTracker::clearConversation(const std::string_view conversationId)
{
    std::erase_if(m_commands, [conversationId](const AiTrackedCommand &command) {
        return command.conversationId == conversationId;
    });
}

std::size_t AiCommandTracker::size() const noexcept
{
    return m_commands.size();
}

bool AiCommandTracker::makeRoom()
{
    const auto removable = std::ranges::find_if(m_commands, [](const AiTrackedCommand &command) {
        return command.state == AiTrackedCommandState::finished || command.state == AiTrackedCommandState::disconnected
               || command.state == AiTrackedCommandState::outcomeUnknown;
    });
    if (removable == m_commands.end())
    {
        return false;
    }
    m_commands.erase(removable);
    return true;
}

} // namespace ztermy::ai
