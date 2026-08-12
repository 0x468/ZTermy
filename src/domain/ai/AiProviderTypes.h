#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ztermy::ai
{

enum class AiProviderKind : std::uint8_t
{
    openAiResponses,
    anthropicMessages,
    ollama,
    openAiCompatible,
};

enum class AiMessageRole : std::uint8_t
{
    system,
    user,
    assistant,
    tool,
};

struct AiChatMessage final
{
    AiMessageRole role = AiMessageRole::user;
    std::string content;
    std::string toolCallId;
};

struct AiProviderConfiguration final
{
    AiProviderKind kind = AiProviderKind::openAiResponses;
    std::string baseUrl;
    std::string endpointPath;
    std::string model;
};

struct AiToolDefinition final
{
    std::string name;
    std::string description;
    std::string parametersJson;
};

struct AiToolCall final
{
    std::string id;
    std::string name;
    std::string argumentsJson;
};

struct AiToolOutput final
{
    std::string callId;
    std::string name;
    std::string outputJson;
};

struct AiToolExchange final
{
    std::vector<AiToolCall> calls;
    std::vector<AiToolOutput> outputs;
    std::string reasoning;
};

struct AiGenerationRequest final
{
    std::string instructions;
    std::vector<AiChatMessage> messages;
    std::vector<AiToolDefinition> tools;
    std::vector<AiToolExchange> toolHistory;
    std::optional<std::string> previousResponseId;
};

enum class AiStreamEventType : std::uint8_t
{
    responseStarted,
    textDelta,
    reasoningDelta,
    toolCallStarted,
    toolArgumentsDelta,
    toolCallCompleted,
    usageUpdated,
    responseCompleted,
    responseFailed,
};

enum class AiProviderErrorCode : std::uint8_t
{
    network,
    authentication,
    rateLimited,
    quotaExceeded,
    invalidRequest,
    server,
    cancelled,
    protocol,
};

struct AiTokenUsage final
{
    std::uint64_t inputTokens = 0;
    std::uint64_t outputTokens = 0;
    std::uint64_t reasoningTokens = 0;
    std::uint64_t cachedInputTokens = 0;
};

struct AiProviderError final
{
    AiProviderErrorCode code = AiProviderErrorCode::protocol;
    std::string message;
    std::optional<std::uint64_t> retryAfterMilliseconds;
    bool retryable = false;
};

struct AiStreamEvent final
{
    AiStreamEventType type = AiStreamEventType::responseStarted;
    std::string responseId;
    std::string itemId;
    std::string toolCallId;
    std::string toolName;
    std::string delta;
    std::optional<AiTokenUsage> usage;
    std::optional<AiProviderError> error;
};

} // namespace ztermy::ai
