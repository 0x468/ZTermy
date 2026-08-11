#include "domain/ai/ServerSentEventParser.h"

#include <algorithm>
#include <charconv>
#include <utility>

namespace ztermy::ai
{

ServerSentEventParser::ServerSentEventParser(const ServerSentEventParserLimits limits)
    : m_limits(limits)
{
}

std::expected<std::vector<ServerSentEvent>, StreamParseError>
ServerSentEventParser::append(const std::string_view bytes)
{
    if (m_failed)
    {
        return std::unexpected(m_error);
    }
    if (bytes.size() > m_limits.maxBufferedBytes - std::min(m_buffer.size(), m_limits.maxBufferedBytes))
    {
        return fail(StreamParseErrorCode::bufferTooLarge);
    }

    m_buffer.append(bytes);
    std::vector<ServerSentEvent> events;
    auto newline = m_buffer.find('\n');
    while (newline != std::string::npos)
    {
        auto line = std::string_view(m_buffer).substr(0, newline);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (line.size() > m_limits.maxLineBytes)
        {
            return fail(StreamParseErrorCode::lineTooLarge);
        }
        if (const auto error = processLine(line, events); error.has_value())
        {
            return fail(error->code);
        }
        m_buffer.erase(0, newline + 1);
        newline = m_buffer.find('\n');
    }

    if (m_buffer.size() > m_limits.maxLineBytes)
    {
        return fail(StreamParseErrorCode::lineTooLarge);
    }
    return events;
}

std::expected<std::vector<ServerSentEvent>, StreamParseError> ServerSentEventParser::finish()
{
    if (m_failed)
    {
        return std::unexpected(m_error);
    }

    std::vector<ServerSentEvent> events;
    if (!m_buffer.empty())
    {
        auto line = std::string_view(m_buffer);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (line.size() > m_limits.maxLineBytes)
        {
            return fail(StreamParseErrorCode::lineTooLarge);
        }
        if (const auto error = processLine(line, events); error.has_value())
        {
            return fail(error->code);
        }
        m_buffer.clear();
    }
    dispatch(events);
    return events;
}

void ServerSentEventParser::reset() noexcept
{
    m_buffer.clear();
    m_eventName.clear();
    m_data.clear();
    m_lastEventId.clear();
    m_retryMilliseconds.reset();
    m_hasData = false;
    m_failed = false;
    m_error = {};
}

std::optional<StreamParseError> ServerSentEventParser::processLine(const std::string_view line,
                                                                   std::vector<ServerSentEvent> &events)
{
    if (line.empty())
    {
        dispatch(events);
        return std::nullopt;
    }
    if (line.front() == ':')
    {
        return std::nullopt;
    }

    const auto separator = line.find(':');
    const auto field = line.substr(0, separator);
    auto value = separator == std::string_view::npos ? std::string_view{} : line.substr(separator + 1);
    if (!value.empty() && value.front() == ' ')
    {
        value.remove_prefix(1);
    }

    if (field == "data")
    {
        if (value.size() + 1 > m_limits.maxEventBytes - std::min(m_data.size(), m_limits.maxEventBytes))
        {
            return StreamParseError{StreamParseErrorCode::eventTooLarge};
        }
        m_data.append(value);
        m_data.push_back('\n');
        m_hasData = true;
    }
    else if (field == "event")
    {
        m_eventName.assign(value);
    }
    else if (field == "id" && value.find('\0') == std::string_view::npos)
    {
        m_lastEventId.assign(value);
    }
    else if (field == "retry")
    {
        std::uint64_t retry = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), retry);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
        {
            m_retryMilliseconds = retry;
        }
    }
    return std::nullopt;
}

void ServerSentEventParser::dispatch(std::vector<ServerSentEvent> &events)
{
    if (m_hasData)
    {
        if (!m_data.empty())
        {
            m_data.pop_back();
        }
        ServerSentEvent event;
        event.event = m_eventName.empty() ? "message" : std::move(m_eventName);
        event.data = std::move(m_data);
        event.id = m_lastEventId;
        event.retryMilliseconds = m_retryMilliseconds;
        events.push_back(std::move(event));
    }
    m_eventName.clear();
    m_data.clear();
    m_retryMilliseconds.reset();
    m_hasData = false;
}

std::expected<std::vector<ServerSentEvent>, StreamParseError>
ServerSentEventParser::fail(const StreamParseErrorCode code)
{
    m_failed = true;
    m_error = StreamParseError{code};
    return std::unexpected(m_error);
}

} // namespace ztermy::ai
