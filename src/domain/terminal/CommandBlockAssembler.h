#pragma once

#include "domain/terminal/CommandBlockStore.h"
#include "domain/terminal/ShellIntegrationDecoder.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace ztermy::terminal
{

struct CommandBlockSessionContext final
{
    std::string sessionId;
    std::string host;
    std::string shell;
    std::uint64_t sessionGeneration = 0;
};

class CommandBlockAssembler final
{
public:
    CommandBlockAssembler(CommandBlockStore &store, CommandBlockSessionContext context);

    [[nodiscard]] std::expected<void, CommandBlockStoreError> observe(std::span<const ShellIntegrationEvent> events,
                                                                      std::int64_t observedUtcMs);
    [[nodiscard]] std::expected<void, CommandBlockStoreError> finishActive(CommandCompletionReason reason,
                                                                           std::int64_t finishedUtcMs);

    void observeFallbackCommand(std::string command);
    [[nodiscard]] std::optional<CommandBlockId> activeBlockId() const noexcept;
    [[nodiscard]] const std::string &workingDirectory() const noexcept;
    [[nodiscard]] bool richCapabilityClaimed() const noexcept;

private:
    [[nodiscard]] std::expected<void, CommandBlockStoreError> begin(const ShellIntegrationEvent &event,
                                                                    std::int64_t startedUtcMs);
    void resetPendingCommand();

    CommandBlockStore &m_store;
    CommandBlockSessionContext m_context;
    std::optional<CommandBlockId> m_activeBlockId;
    std::string m_workingDirectory;
    std::string m_pendingCommand;
    CommandProvenance m_pendingProvenance = CommandProvenance::unknown;
    bool m_pendingCommandVerified = false;
    bool m_richCapabilityClaimed = false;
};

} // namespace ztermy::terminal
