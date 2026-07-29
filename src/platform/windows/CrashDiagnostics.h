#pragma once

#include <QString>

namespace ztermy::diagnostics
{

void initialize(const QString &directory = {});
[[nodiscard]] QString crashDirectoryPath();

} // namespace ztermy::diagnostics
