#pragma once

#include <QString>

namespace ztermy::logging
{

void initialize(const QString &logsDirectory = {});
[[nodiscard]] QString logFilePath();

} // namespace ztermy::logging
