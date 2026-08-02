#pragma once

#include "domain/workbench/ShellHistory.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::workbench
{

enum class PowerShellHistoryReadError : std::uint8_t
{
    invalidPath,
    ioError,
};

[[nodiscard]] QString defaultPowerShellHistoryPath();
[[nodiscard]] std::expected<std::vector<ShellHistoryEntry>, PowerShellHistoryReadError>
readPowerShellHistory(const QString &path, std::size_t maximumEntries = 1000,
                      qint64 maximumSourceBytes = qint64{2} * 1024 * 1024);

} // namespace ztermy::workbench
