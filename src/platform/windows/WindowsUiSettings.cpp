#include "platform/windows/WindowsUiSettings.h"

#include <Windows.h>

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

} // namespace ztermy::windowing
