#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/ServerSentEventParser.h"

#include <QJsonObject>

#include <cstddef>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ztermy::ai
{

class OpenAiResponsesStreamMapper final
{
public:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, AiProviderError> map(const ServerSentEvent &event);
    void reset() noexcept;

private:
    struct ToolState final
    {
        std::string callId;
        std::string name;
    };

    std::unordered_map<std::string, ToolState> m_toolsByItemId;
    std::unordered_map<std::string, std::string> m_webQueriesByItemId;
};

class OpenAiCompatibleStreamMapper final
{
public:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, AiProviderError> map(const ServerSentEvent &event);
    void reset() noexcept;

private:
    struct ToolState final
    {
        std::string callId;
        std::string name;
    };

    std::unordered_map<std::size_t, ToolState> m_toolsByIndex;
    bool m_started = false;
    bool m_completed = false;
};

class AnthropicStreamMapper final
{
public:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, AiProviderError> map(const ServerSentEvent &event);
    void reset() noexcept;

private:
    struct ToolState final
    {
        std::string callId;
        std::string name;
    };

    struct ContentBlockState final
    {
        QJsonObject block;
        std::string partialInputJson;
    };

    std::unordered_map<std::size_t, ToolState> m_toolsByIndex;
    std::unordered_map<std::size_t, ToolState> m_webSearchByIndex;
    std::unordered_map<std::size_t, std::string> m_webQueriesByIndex;
    std::map<std::size_t, ContentBlockState> m_contentBlocksByIndex;
    std::string m_responseId;
    AiResponseStopReason m_stopReason = AiResponseStopReason::unspecified;
    std::size_t m_contentBytes = 0;
    bool m_started = false;
    bool m_completed = false;
};

class OllamaStreamMapper final
{
public:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, AiProviderError> map(std::string_view line);
    void reset() noexcept;

private:
    std::uint64_t m_toolSequence = 0;
    bool m_started = false;
    bool m_completed = false;
};

} // namespace ztermy::ai
