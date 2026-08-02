#include "platform/windows/WindowsUiSettings.h"

#include <Windows.h>
#include <dwmapi.h>

namespace ztermy::windowing
{
namespace
{

[[nodiscard]] RgbColor systemColor(const int index) noexcept
{
    const COLORREF color = GetSysColor(index);
    return {
        .red = GetRValue(color),
        .green = GetGValue(color),
        .blue = GetBValue(color),
    };
}

} // namespace

std::optional<bool> queryClientAreaAnimationsEnabled() noexcept
{
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) == FALSE)
    {
        return std::nullopt;
    }
    return enabled != FALSE;
}

std::optional<HighContrastState> queryHighContrastState() noexcept
{
    HIGHCONTRASTW highContrast{.cbSize = sizeof(HIGHCONTRASTW)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRASTW), &highContrast, 0) == FALSE)
    {
        return std::nullopt;
    }
    return HighContrastState{
        .enabled = (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0,
        .background = systemColor(COLOR_WINDOW),
        .text = systemColor(COLOR_WINDOWTEXT),
        .highlight = systemColor(COLOR_HIGHLIGHT),
        .highlightText = systemColor(COLOR_HIGHLIGHTTEXT),
    };
}

std::optional<RgbColor> querySystemAccentColor() noexcept
{
    DWORD colorizationColor = 0;
    BOOL opaqueBlend = FALSE;
    if (FAILED(DwmGetColorizationColor(&colorizationColor, &opaqueBlend)))
    {
        return std::nullopt;
    }
    return decodeColorizationArgb(colorizationColor);
}

} // namespace ztermy::windowing
