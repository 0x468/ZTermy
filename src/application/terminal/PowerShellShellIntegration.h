#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ztermy::terminal
{

[[nodiscard]] std::optional<std::wstring> powerShellLaunchCommand(std::wstring_view executable, std::string_view nonce);

} // namespace ztermy::terminal
