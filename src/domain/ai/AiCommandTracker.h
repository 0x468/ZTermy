#pragma once

#include "domain/ai/AiAgentGuard.h"
#include "domain/terminal/CommandBlockStore.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AiTrackedCommandState : std::uint8_t
{
    queued,
    running,
    finished,
    disconnected,
    outcomeUnknown,
};

struct AiTrackedCommand final
{
    std::string id;
    std::string conversationId;
    AiSessionTarget target;
    std::string command;
    terminal::CommandBlockId baselineBlockId = 0;
    std::optional<terminal::CommandBlockId> blockId;
    AiTrackedCommandState state = AiTrackedCommandState::queued;
    std::optional<int> exitStatus;
    bool interruptRequested = false;
};

class AiCommandTracker final
{
public:
    explicit AiCommandTracker(std::size_t maximumCommands = 64);

    [[nodiscard]] bool accept(AiTrackedCommand command);
    [[nodiscard]] std::optional<AiTrackedCommand>
    observe(std::string_view commandId, std::span<const terminal::CommandBlock> blocks, bool connected);
    [[nodiscard]] std::optional<AiTrackedCommand> find(std::string_view commandId) const;
    [[nodiscard]] bool markInterruptRequested(std::string_view commandId);
    void clearConversation(std::string_view conversationId);
    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] bool makeRoom();

    std::size_t m_maximumCommands;
    std::vector<AiTrackedCommand> m_commands;
};

} // namespace ztermy::ai
