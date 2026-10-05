#pragma once

#include <Windows.h>

namespace ztermy::windowing
{
inline constexpr const wchar_t *taskbarApplicationId = L"ZSeries.Ztermy";
[[nodiscard]] bool initializeTaskbarIdentity() noexcept;
[[nodiscard]] bool configureTaskbarIdentity(HWND window);
[[nodiscard]] bool hasTaskbarIdentity(HWND window);
} // namespace ztermy::windowing
