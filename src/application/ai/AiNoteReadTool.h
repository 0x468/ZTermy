#pragma once

#include "application/ai/AiActionToolDispatcher.h"
#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/workbench/NoteStore.h"

#include <QString>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiNoteReadRequest final
{
    AiSessionTarget target;
    QString relativePath;
    std::size_t maximumBytes = 0;
};

class AiNoteReadTool final
{
public:
    [[nodiscard]] static AiToolDefinition definition();
    [[nodiscard]] static std::expected<AiNoteReadRequest, std::string> parse(std::string_view argumentsJson);
    [[nodiscard]] static std::string result(const AiNoteReadRequest &request, const QString &content);
    [[nodiscard]] static std::string failure(workbench::NoteStoreError error);
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
};

} // namespace ztermy::ai
