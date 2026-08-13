#include "domain/terminal/SemanticTerminalObserver.h"

#include <chrono>
#include <utility>

namespace ztermy::terminal
{

SemanticTerminalObserver::SemanticTerminalObserver(CommandBlockSessionContext context, std::string expectedNonce,
                                                   CommandBlockStoreLimits storeLimits,
                                                   ShellIntegrationDecoderLimits decoderLimits, Clock clock)
    : m_expectedNonce(std::move(expectedNonce)),
      m_clock(std::move(clock)),
      m_decoder(m_expectedNonce, decoderLimits),
      m_store(storeLimits),
      m_assembler(m_store, std::move(context))
{
    if (!m_clock)
    {
        m_clock = &SemanticTerminalObserver::currentUtcMs;
    }
}

void SemanticTerminalObserver::append(const std::span<const std::byte> bytes) noexcept
{
    if (bytes.empty())
    {
        return;
    }
    try
    {
        std::scoped_lock lock(m_mutex);
        if (m_finished)
        {
            return;
        }
        const std::vector<ShellIntegrationEvent> events = m_decoder.append(bytes);
        record(m_assembler.observe(events, m_clock()));
    }
    catch (...)
    {
        std::scoped_lock lock(m_mutex);
        m_internalFailure = true;
    }
}

void SemanticTerminalObserver::observeFallbackCommand(std::string command) noexcept
{
    try
    {
        std::scoped_lock lock(m_mutex);
        if (!m_finished)
        {
            m_assembler.observeFallbackCommand(std::move(command));
        }
    }
    catch (...)
    {
        std::scoped_lock lock(m_mutex);
        m_internalFailure = true;
    }
}

void SemanticTerminalObserver::finish(const CommandCompletionReason reason) noexcept
{
    try
    {
        std::scoped_lock lock(m_mutex);
        if (m_finished)
        {
            return;
        }
        const std::int64_t now = m_clock();
        const std::vector<ShellIntegrationEvent> trailing = m_decoder.finish();
        record(m_assembler.observe(trailing, now));
        record(m_assembler.finishActive(reason, now));
        m_finished = true;
    }
    catch (...)
    {
        std::scoped_lock lock(m_mutex);
        m_internalFailure = true;
        m_finished = true;
    }
}

SemanticTerminalSnapshot SemanticTerminalObserver::snapshot() const
{
    std::scoped_lock lock(m_mutex);
    const auto &blocks = m_store.blocks();
    return SemanticTerminalSnapshot{
        .commandBlocks = std::vector<CommandBlock>(blocks.begin(), blocks.end()),
        .lastStoreError = m_lastStoreError,
        .rawStreamOffset = m_decoder.rawStreamOffset(),
        .outputStreamOffset = m_decoder.outputStreamOffset(),
        .richCapabilityClaimed = m_assembler.richCapabilityClaimed(),
        .internalFailure = m_internalFailure,
        .finished = m_finished,
    };
}

const std::string &SemanticTerminalObserver::expectedNonce() const noexcept
{
    return m_expectedNonce;
}

std::int64_t SemanticTerminalObserver::currentUtcMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void SemanticTerminalObserver::record(std::expected<void, CommandBlockStoreError> result) noexcept
{
    if (!result)
    {
        m_lastStoreError = result.error();
    }
}

} // namespace ztermy::terminal
