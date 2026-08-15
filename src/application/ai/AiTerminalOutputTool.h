#pragma once

#include "domain/ai/AiAgentGuard.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/terminal/TerminalEngine.h"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiTerminalOutputRequest final
{
    AiSessionTarget target;
    terminal::TerminalScrollbackAnchor anchor = terminal::TerminalScrollbackAnchor::tail;
    std::size_t offset = 0;
    std::size_t lineCount = 1;
    std::size_t maximumBytes = std::size_t{16} * 1024;
};

struct AiTerminalOutputRead final
{
    std::string content;
    std::size_t firstLine = 0;
    std::size_t lineCount = 0;
    std::size_t totalLines = 0;
    std::size_t scrollbackLines = 0;
    std::size_t nextOffset = 0;
    std::size_t bytesRead = 0;
    bool hasMore = false;
    bool truncated = false;
    bool partialLine = false;
};

class AiTerminalOutputTool final
{
public:
    [[nodiscard]] static AiToolDefinition definition();
    [[nodiscard]] static std::expected<AiTerminalOutputRequest, std::string> parse(std::string_view argumentsJson,
                                                                                   const AiSessionTarget &target);
    [[nodiscard]] static AiTerminalOutputRead read(const AiTerminalOutputRequest &request,
                                                   const terminal::TerminalScrollbackPage &page);
    [[nodiscard]] static std::string result(const AiTerminalOutputRequest &request,
                                            const terminal::TerminalScrollbackPage &page);
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
};

} // namespace ztermy::ai
