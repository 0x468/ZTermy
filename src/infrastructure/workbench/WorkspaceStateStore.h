#pragma once

#include "domain/workbench/WorkspaceState.h"

#include <QByteArray>
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
    // Serializes and writes the state. An unchanged state is not rewritten,
    // and the ".bak" copy is produced from the payload this store last
    // wrote or loaded instead of re-reading and re-parsing the file.
    [[nodiscard]] std::expected<void, WorkspaceStateStoreError> save(const WorkspaceState &state) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
    mutable QByteArray m_knownGoodPayload;
};

} // namespace ztermy::workbench
