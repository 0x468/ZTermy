#pragma once

#include "domain/ai/ServerSentEventParser.h"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct NdjsonParserLimits final
{
    std::size_t maxLineBytes = std::size_t{1024} * 1024;
    std::size_t maxBufferedBytes = std::size_t{2} * 1024 * 1024;
};

class NdjsonParser final
{
public:
    explicit NdjsonParser(NdjsonParserLimits limits = {});

    [[nodiscard]] std::expected<std::vector<std::string>, StreamParseError> append(std::string_view bytes);
    [[nodiscard]] std::expected<std::vector<std::string>, StreamParseError> finish();
    void reset() noexcept;

private:
    [[nodiscard]] std::expected<std::vector<std::string>, StreamParseError> fail(StreamParseErrorCode code);
    static void appendLine(std::string_view line, std::vector<std::string> &lines);

    NdjsonParserLimits m_limits;
    std::string m_buffer;
    bool m_failed = false;
    StreamParseError m_error;
};

} // namespace ztermy::ai
