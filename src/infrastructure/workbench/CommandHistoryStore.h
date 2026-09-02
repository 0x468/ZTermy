#pragma once

#include "domain/workbench/CommandHistoryIndex.h"

#include <QString>

#include <cstdint>
#include <expected>

namespace ztermy::workbench
{

enum class CommandHistoryStoreError : std::uint8_t
{
    io,
    corrupt,
    unsupportedVersion,
};

class CommandHistoryStore final
{
public:
    explicit CommandHistoryStore(QString path);

    [[nodiscard]] std::expected<CommandHistoryIndex, CommandHistoryStoreError> load() const;
    [[nodiscard]] std::expected<void, CommandHistoryStoreError> save(const CommandHistoryIndex &index) const;

private:
    QString m_path;
};

} // namespace ztermy::workbench
