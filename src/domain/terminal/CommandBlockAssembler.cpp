#include "domain/terminal/CommandBlockAssembler.h"

#include <utility>

namespace ztermy::terminal
{

CommandBlockAssembler::CommandBlockAssembler(CommandBlockStore &store, CommandBlockSessionContext context)
    : m_store(store), m_context(std::move(context))
{
}

std::expected<void, CommandBlockStoreError>
CommandBlockAssembler::observe(const std::span<const ShellIntegrationEvent> events, const std::int64_t observedUtcMs)
{
    for (const ShellIntegrationEvent &event : events)
    {
        switch (event.type)
        {
            case ShellIntegrationEventType::output:
                if (m_activeBlockId)
                {
                    auto appended = m_store.append(*m_activeBlockId, CommandOutputObservation{
                                                                         .bytes = event.output,
                                                                         .streamOffset = event.outputStreamOffset,
                                                                     });
                    if (!appended)
                    {
                        return appended;
                    }
                }
                break;
            case ShellIntegrationEventType::promptStart:
                if (m_activeBlockId)
                {
                    auto finished = finishActive(CommandCompletionReason::promptRecovery, observedUtcMs);
                    if (!finished)
                    {
                        return finished;
                    }
                }
                break;
            case ShellIntegrationEventType::commandStart:
                resetPendingCommand();
                break;
            case ShellIntegrationEventType::explicitCommand:
                m_pendingCommand = event.value;
                m_pendingProvenance =
                    event.nonceVerified ? CommandProvenance::verifiedShellIntegration : CommandProvenance::osc633;
                m_pendingCommandVerified = event.nonceVerified;
                break;
            case ShellIntegrationEventType::commandExecuted:
                if (m_activeBlockId)
                {
                    auto recovered = finishActive(CommandCompletionReason::promptRecovery, observedUtcMs);
                    if (!recovered)
                    {
                        return recovered;
                    }
                }
                if (auto started = begin(event, observedUtcMs); !started)
                {
                    return started;
                }
                break;
            case ShellIntegrationEventType::commandFinished:
                if (m_activeBlockId)
                {
                    auto finished = m_store.finish(*m_activeBlockId, event.exitStatus, observedUtcMs,
                                                   CommandCompletionReason::shellMarker);
                    if (!finished)
                    {
                        return finished;
                    }
                    m_activeBlockId.reset();
                }
                resetPendingCommand();
                break;
            case ShellIntegrationEventType::workingDirectory:
                m_workingDirectory = event.value;
                break;
            case ShellIntegrationEventType::richCapabilityClaim:
                m_richCapabilityClaimed = true;
                break;
            case ShellIntegrationEventType::decoderError:
                if (m_activeBlockId)
                {
                    auto uncertain = m_store.markOutputUnknown(*m_activeBlockId);
                    if (!uncertain)
                    {
                        return uncertain;
                    }
                }
                m_pendingCommandVerified = false;
                break;
        }
    }
    return {};
}

std::expected<void, CommandBlockStoreError> CommandBlockAssembler::finishActive(const CommandCompletionReason reason,
                                                                                const std::int64_t finishedUtcMs)
{
    if (!m_activeBlockId)
    {
        return {};
    }
    auto finished = m_store.finish(*m_activeBlockId, std::nullopt, finishedUtcMs, reason);
    if (!finished)
    {
        return finished;
    }
    m_activeBlockId.reset();
    resetPendingCommand();
    return {};
}

void CommandBlockAssembler::observeFallbackCommand(std::string command)
{
    if (m_pendingProvenance == CommandProvenance::verifiedShellIntegration
        || m_pendingProvenance == CommandProvenance::osc633)
    {
        return;
    }
    m_pendingCommand = std::move(command);
    m_pendingProvenance = CommandProvenance::heuristicInput;
    m_pendingCommandVerified = false;
}

std::optional<CommandBlockId> CommandBlockAssembler::activeBlockId() const noexcept
{
    return m_activeBlockId;
}

const std::string &CommandBlockAssembler::workingDirectory() const noexcept
{
    return m_workingDirectory;
}

bool CommandBlockAssembler::richCapabilityClaimed() const noexcept
{
    return m_richCapabilityClaimed;
}

std::expected<void, CommandBlockStoreError> CommandBlockAssembler::begin(const ShellIntegrationEvent &event,
                                                                         const std::int64_t startedUtcMs)
{
    CommandProvenance provenance = m_pendingProvenance;
    if (provenance == CommandProvenance::unknown)
    {
        provenance =
            event.protocol == ShellIntegrationProtocol::osc633 ? CommandProvenance::osc633 : CommandProvenance::osc133;
    }
    const TerminalSemanticCapability capability =
        m_pendingCommandVerified ? TerminalSemanticCapability::rich : TerminalSemanticCapability::basic;
    const CommandBoundaryConfidence confidence =
        m_pendingCommand.empty() ? CommandBoundaryConfidence::heuristic : CommandBoundaryConfidence::exact;
    auto started = m_store.begin(CommandBlockStart{
        .command = m_pendingCommand,
        .workingDirectory = m_workingDirectory,
        .sessionId = m_context.sessionId,
        .host = m_context.host,
        .shell = m_context.shell,
        .sessionGeneration = m_context.sessionGeneration,
        .capability = capability,
        .commandProvenance = provenance,
        .boundaryConfidence = confidence,
        .startedUtcMs = startedUtcMs,
        .outputStreamOffset = event.outputStreamOffset,
    });
    if (!started)
    {
        return std::unexpected(started.error());
    }
    m_activeBlockId = *started;
    return {};
}

void CommandBlockAssembler::resetPendingCommand()
{
    m_pendingCommand.clear();
    m_pendingProvenance = CommandProvenance::unknown;
    m_pendingCommandVerified = false;
}

} // namespace ztermy::terminal
