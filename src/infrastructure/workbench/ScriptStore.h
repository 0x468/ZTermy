#pragma once

#include "domain/workbench/ScriptDefinition.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::workbench
{

enum class ScriptStoreError : std::uint8_t
{
    invalidPath,
    ioError,
    invalidFormat,
    unsupportedVersion,
};

class ScriptStore final
{
public:
    explicit ScriptStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<std::vector<ScriptDefinition>, ScriptStoreError> load() const;
    [[nodiscard]] std::expected<std::vector<ScriptDefinition>, ScriptStoreError>
    loadOrMigrate(const QString &legacyQuickCommandPath) const;
    [[nodiscard]] std::expected<void, ScriptStoreError> save(std::span<const ScriptDefinition> scripts) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::workbench
