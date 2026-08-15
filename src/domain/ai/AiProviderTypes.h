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

enum class AiProviderFlavor : std::uint8_t
{
    openAi,
    anthropic,
    deepSeek,
    kimi,
    zai,
    ollama,
    compatible,
};

enum class AiReasoningEffort : std::uint8_t
{
    automatic,
    disabled,
    low,
    medium,
    high,
    maximum,
};

enum class AiMessageRole : std::uint8_t
{
    system,
    user,
    assistant,
    tool,
};

struct AiImageAttachment final
{
    std::string id;
    std::string fileName;
    std::string mediaType;
    std::string base64Data;
    std::string previewBase64Data;
    std::uint64_t byteSize = 0;
    std::uint32_t pixelWidth = 0;
    std::uint32_t pixelHeight = 0;

    [[nodiscard]] friend bool operator==(const AiImageAttachment &, const AiImageAttachment &) = default;
};

struct AiChatMessage final
{
    AiMessageRole role = AiMessageRole::user;
    std::string content;
    std::string toolCallId;
    std::vector<AiImageAttachment> images;
    // Bounded provider-native continuation state owned by this completed
    // assistant message. It is never rendered as user-visible text.
    std::string providerReplayJson;
};

struct AiProviderConfiguration final
{
    AiProviderKind kind = AiProviderKind::openAiResponses;
    AiProviderFlavor flavor = AiProviderFlavor::openAi;
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
    std::string reasoningSignature;
    // Provider-owned assistant content used only for exact continuation of
    // protocols whose tool state lives inside typed content blocks. The JSON
    // is a validated, bounded array and is never shown to the model as text.
    std::string providerAssistantContentJson;
};

struct AiWebSource final
{
    std::string url;
    std::string title;
    std::string citedText;

    [[nodiscard]] friend bool operator==(const AiWebSource &, const AiWebSource &) = default;
};

struct AiGenerationRequest final
{
    std::string instructions;
    std::vector<AiChatMessage> messages;
    std::vector<AiToolDefinition> tools;
    std::vector<AiToolExchange> toolHistory;
    std::optional<std::string> previousResponseId;
    AiReasoningEffort reasoningEffort = AiReasoningEffort::automatic;
    bool webSearchEnabled = false;
};

enum class AiStreamEventType : std::uint8_t
{
    responseStarted,
    textDelta,
    reasoningDelta,
    reasoningSignatureDelta,
    webSearchStarted,
    webSearchQuery,
    webSearchCompleted,
    webSourceAdded,
    toolCallStarted,
    toolArgumentsDelta,
    toolCallCompleted,
    usageUpdated,
    responseCompleted,
    responseFailed,
};

enum class AiResponseStopReason : std::uint8_t
{
    unspecified,
    endTurn,
    maximumTokens,
    stopSequence,
    toolUse,
    pauseTurn,
    refusal,
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
    contextOverflow,
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
    AiResponseStopReason stopReason = AiResponseStopReason::unspecified;
    std::string providerAssistantContentJson;
    // Populated only on the final completion event for providers that require
    // exact client-side replay across later conversation turns.
    std::vector<AiToolExchange> providerToolHistory;
    std::optional<AiWebSource> webSource;
    std::optional<AiTokenUsage> usage;
    std::optional<AiProviderError> error;
};

} // namespace ztermy::ai
