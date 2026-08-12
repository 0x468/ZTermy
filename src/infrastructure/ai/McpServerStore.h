#pragma once

#include "infrastructure/ai/McpStdioClient.h"

#include <QString>

#include <expected>
#include <string>
#include <vector>

namespace ztermy::ai
{

struct McpApprovedTool final
{
    std::string exposedName;
    std::string schemaDigest;

    bool operator==(const McpApprovedTool &) const = default;
};

struct McpServerRecord final
{
    McpStdioConfiguration configuration;
    std::vector<McpApprovedTool> approvedTools;
    bool enabled = false;
};

class McpServerStore final
{
public:
    explicit McpServerStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<McpServerRecord>, QString> load();
    [[nodiscard]] std::expected<void, QString> save(const std::vector<McpServerRecord> &servers) const;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;

private:
    QString m_filePath;
    bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::ai
