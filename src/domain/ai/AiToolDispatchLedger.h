#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct AiToolDispatchKey final
{
    std::string conversationId;
    std::uint64_t turnId = 0;
    std::string toolCallId;

    [[nodiscard]] bool operator==(const AiToolDispatchKey &) const noexcept = default;
};

struct AiToolDispatchRequest final
{
    AiToolDispatchKey key;
    std::string toolName;
    std::string canonicalArguments;
    std::string sessionId;
    std::uint64_t sessionGeneration = 0;
    bool sideEffecting = false;
};

enum class AiToolDispatchState : std::uint8_t
{
    pending,
    awaitingApproval,
    running,
    succeeded,
    failed,
    cancelled,
    outcomeUnknown,
};

struct AiToolDispatchRecord final
{
    AiToolDispatchRequest request;
    AiToolDispatchState state = AiToolDispatchState::pending;
    std::string resultJson;
    std::uint64_t sequence = 0;
};

enum class AiToolDispatchAdmission : std::uint8_t
{
    accepted,
    joined,
    cached,
    duplicateMismatch,
    invalidRequest,
    capacityExceeded,
};

struct AiToolDispatchOutcome final
{
    AiToolDispatchAdmission admission = AiToolDispatchAdmission::invalidRequest;
    std::optional<AiToolDispatchRecord> record;
};

struct AiToolDispatchLimits final
{
    std::size_t maximumRecords = 256;
    std::size_t maximumIdentifierBytes = 256;
    std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
    std::size_t maximumResultBytes = std::size_t{64} * 1024;
};

class AiToolDispatchLedger final
{
public:
    explicit AiToolDispatchLedger(AiToolDispatchLimits limits = {});

    [[nodiscard]] AiToolDispatchOutcome begin(AiToolDispatchRequest request);
    [[nodiscard]] bool transition(const AiToolDispatchKey &key, AiToolDispatchState state, std::string resultJson = {});
    [[nodiscard]] std::optional<AiToolDispatchRecord> find(const AiToolDispatchKey &key) const;
    [[nodiscard]] std::size_t size() const;
    void clearConversation(std::string_view conversationId);

private:
    [[nodiscard]] bool valid(const AiToolDispatchRequest &request) const noexcept;
    [[nodiscard]] static bool terminal(AiToolDispatchState state) noexcept;
    [[nodiscard]] static bool transitionAllowed(AiToolDispatchState from, AiToolDispatchState to) noexcept;
    [[nodiscard]] bool makeRoom();

    AiToolDispatchLimits m_limits;
    mutable std::mutex m_mutex;
    std::vector<AiToolDispatchRecord> m_records;
    std::uint64_t m_nextSequence = 1;
};

} // namespace ztermy::ai
