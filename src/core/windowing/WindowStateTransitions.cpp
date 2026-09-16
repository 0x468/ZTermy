#include "core/windowing/WindowStateTransitions.h"

namespace ztermy::windowing
{
Qt::WindowStates minimizedStates(const Qt::WindowStates current) noexcept
{
    return current | Qt::WindowMinimized;
}

Qt::WindowStates presentedStates(const Qt::WindowStates current) noexcept
{
    return current & ~Qt::WindowMinimized;
}

Qt::WindowStates maximizeToggledStates(const Qt::WindowStates current) noexcept
{
    const Qt::WindowStates presented = presentedStates(current);
    return presented.testFlag(Qt::WindowMaximized) ? presented & ~Qt::WindowMaximized : presented | Qt::WindowMaximized;
}
} // namespace ztermy::windowing
