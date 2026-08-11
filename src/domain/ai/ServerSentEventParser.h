#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct ServerSentEvent final
{
    std::string event = "message";
    std::string data;
    std::string id;
    std::optional<std::uint64_t> retryMilliseconds;
};

struct ServerSentEventParserLimits final
{
    std::size_t maxLineBytes = std::size_t{64} * 1024;
    std::size_t maxEventBytes = std::size_t{1024} * 1024;
    std::size_t maxBufferedBytes = std::size_t{2} * 1024 * 1024;
};

enum class StreamParseErrorCode : std::uint8_t
{
    lineTooLarge,
    eventTooLarge,
    bufferTooLarge,
};

struct StreamParseError final
{
    StreamParseErrorCode code = StreamParseErrorCode::bufferTooLarge;
};

class ServerSentEventParser final
{
public:
    explicit ServerSentEventParser(ServerSentEventParserLimits limits = {});

    [[nodiscard]] std::expected<std::vector<ServerSentEvent>, StreamParseError> append(std::string_view bytes);
    [[nodiscard]] std::expected<std::vector<ServerSentEvent>, StreamParseError> finish();
    void reset() noexcept;

private:
    [[nodiscard]] std::optional<StreamParseError> processLine(std::string_view line,
                                                              std::vector<ServerSentEvent> &events);
    void dispatch(std::vector<ServerSentEvent> &events);
    [[nodiscard]] std::expected<std::vector<ServerSentEvent>, StreamParseError> fail(StreamParseErrorCode code);

    ServerSentEventParserLimits m_limits;
    std::string m_buffer;
    std::string m_eventName;
    std::string m_data;
    std::string m_lastEventId;
    std::optional<std::uint64_t> m_retryMilliseconds;
    bool m_hasData = false;
    bool m_failed = false;
    StreamParseError m_error;
};

} // namespace ztermy::ai
