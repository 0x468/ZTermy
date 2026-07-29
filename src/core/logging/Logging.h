#pragma once

#include <QString>

namespace ztermy::logging
{

void initialize();
[[nodiscard]] QString logFilePath();

} // namespace ztermy::logging
