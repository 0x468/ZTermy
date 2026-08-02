#pragma once

#include "domain/workbench/QuickCommand.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::workbench
{

inline constexpr qsizetype maximumQuickCommandCount = 1000;

enum class QuickCommandStoreError : std::uint8_t
{
    invalidPath,
    ioError,
    invalidFormat,
    unsupportedVersion,
};

class QuickCommandStore final
{
public:
    explicit QuickCommandStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<QuickCommand>, QuickCommandStoreError> load() const;
    [[nodiscard]] std::expected<void, QuickCommandStoreError> save(std::span<const QuickCommand> quickCommands) const;

private:
    QString m_filePath;
};

} // namespace ztermy::workbench
