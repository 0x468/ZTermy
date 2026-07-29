#pragma once

#include <QString>

namespace ztermy::diagnostics
{

void initialize();
[[nodiscard]] QString crashDirectoryPath();

} // namespace ztermy::diagnostics
