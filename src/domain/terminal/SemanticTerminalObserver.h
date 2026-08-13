#pragma once

#include "domain/terminal/CommandBlockAssembler.h"
#include "domain/terminal/CommandBlockStore.h"
#include "domain/terminal/ShellIntegrationDecoder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ztermy::terminal
{

struct SemanticTerminalSnapshot final
{
    std::vector<CommandBlock> commandBlocks;
    std::optional<CommandBlockStoreError> lastStoreError;
    std::uint64_t rawStreamOffset = 0;
    std::uint64_t outputStreamOffset = 0;
    bool richCapabilityClaimed = false;
    bool internalFailure = false;
    bool finished = false;
};

class SemanticTerminalObserver final
{
public:
    using Clock = std::function<std::int64_t()>;

    SemanticTerminalObserver(CommandBlockSessionContext context, std::string expectedNonce,
                             CommandBlockStoreLimits storeLimits = {}, ShellIntegrationDecoderLimits decoderLimits = {},
                             Clock clock = {});

    void append(std::span<const std::byte> bytes) noexcept;
    void observeFallbackCommand(std::string command) noexcept;
    void finish(CommandCompletionReason reason = CommandCompletionReason::disconnect) noexcept;

    [[nodiscard]] SemanticTerminalSnapshot snapshot() const;
    [[nodiscard]] const std::string &expectedNonce() const noexcept;

private:
    [[nodiscard]] static std::int64_t currentUtcMs() noexcept;
    void record(std::expected<void, CommandBlockStoreError> result) noexcept;

    mutable std::mutex m_mutex;
    std::string m_expectedNonce;
    Clock m_clock;
    ShellIntegrationDecoder m_decoder;
    CommandBlockStore m_store;
    CommandBlockAssembler m_assembler;
    std::optional<CommandBlockStoreError> m_lastStoreError;
    bool m_internalFailure = false;
    bool m_finished = false;
};

} // namespace ztermy::terminal
