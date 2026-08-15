#pragma once

#include "domain/ai/AiUserSkill.h"

#include <QString>

#include <cstddef>
#include <expected>
#include <vector>

namespace ztermy::ai
{

enum class AiUserSkillCatalogError : std::uint8_t
{
    directoryUnavailable,
    directoryUnreadable,
};

struct AiUserSkillScanResult final
{
    std::vector<AiUserSkill> skills;
    std::size_t readyCount = 0;
    std::size_t warningCount = 0;
};

class AiUserSkillCatalog final
{
public:
    explicit AiUserSkillCatalog(const QString &rootPath, AiUserSkillLimits limits = {});

    [[nodiscard]] const QString &rootPath() const noexcept;
    [[nodiscard]] std::expected<AiUserSkillScanResult, AiUserSkillCatalogError> scan() const;

private:
    QString m_rootPath;
    AiUserSkillLimits m_limits;
};

} // namespace ztermy::ai
