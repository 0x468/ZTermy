#include "domain/ai/NdjsonParser.h"

#include <algorithm>

namespace ztermy::ai
{

NdjsonParser::NdjsonParser(const NdjsonParserLimits limits) : m_limits(limits) {}

std::expected<std::vector<std::string>, StreamParseError> NdjsonParser::append(const std::string_view bytes)
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
    std::vector<std::string> lines;
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
        appendLine(line, lines);
        m_buffer.erase(0, newline + 1);
        newline = m_buffer.find('\n');
    }

    if (m_buffer.size() > m_limits.maxLineBytes)
    {
        return fail(StreamParseErrorCode::lineTooLarge);
    }
    return lines;
}

std::expected<std::vector<std::string>, StreamParseError> NdjsonParser::finish()
{
    if (m_failed)
    {
        return std::unexpected(m_error);
    }
    if (m_buffer.size() > m_limits.maxLineBytes)
    {
        return fail(StreamParseErrorCode::lineTooLarge);
    }

    std::vector<std::string> lines;
    auto line = std::string_view(m_buffer);
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }
    appendLine(line, lines);
    m_buffer.clear();
    return lines;
}

void NdjsonParser::reset() noexcept
{
    m_buffer.clear();
    m_failed = false;
    m_error = {};
}

std::expected<std::vector<std::string>, StreamParseError> NdjsonParser::fail(const StreamParseErrorCode code)
{
    m_failed = true;
    m_error = StreamParseError{code};
    return std::unexpected(m_error);
}

void NdjsonParser::appendLine(const std::string_view line, std::vector<std::string> &lines)
{
    if (!line.empty())
    {
        lines.emplace_back(line);
    }
}

} // namespace ztermy::ai
