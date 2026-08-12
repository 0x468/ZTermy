#pragma once

#include "domain/ai/AiPermissionPolicy.h"

#include <QString>

#include <expected>
#include <vector>

namespace ztermy::ai
{

class AiPermissionRuleStore final
{
public:
    explicit AiPermissionRuleStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<AiPermissionRule>, QString> load();
    [[nodiscard]] std::expected<void, QString> save(const std::vector<AiPermissionRule> &rules) const;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;

private:
    QString m_filePath;
    bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::ai
