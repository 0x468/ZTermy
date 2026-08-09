#pragma once

#include "domain/workbench/WorkspaceState.h"

#include <QString>

#include <cstdint>
#include <expected>

namespace ztermy::workbench
{

enum class WorkspaceStateStoreError : std::uint8_t
{
    Io,
    InvalidDocument,
    UnsupportedVersion,
};

class WorkspaceStateStore final
{
public:
    explicit WorkspaceStateStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<WorkspaceState, WorkspaceStateStoreError> load() const;
    [[nodiscard]] std::expected<void, WorkspaceStateStoreError> save(const WorkspaceState &state) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::workbench
