#include "platform/windows/WindowsUiSettings.h"

#include <Windows.h>
#include <dwmapi.h>

namespace ztermy::windowing
{

std::optional<bool> queryClientAreaAnimationsEnabled() noexcept
{
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) == FALSE)
    {
        return std::nullopt;
    }
    return enabled != FALSE;
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
