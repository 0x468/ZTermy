#include "ui/WindowControl.h"

#include "core/windowing/WindowPresenter.h"

namespace ztermy::ui
{
WindowControl::WindowControl(QObject *parent) : QObject(parent) {}

void WindowControl::minimize(QWindow *window) const
{
    if (window != nullptr)
    {
        windowing::minimize(*window);
    }
}

void WindowControl::toggleMaximize(QWindow *window) const
{
    if (window != nullptr)
    {
        windowing::toggleMaximize(*window);
    }
}

void WindowControl::reveal(QWindow *window) const
{
    if (window != nullptr)
    {
        windowing::reveal(*window);
    }
}

void WindowControl::present(QWindow *window) const
{
    if (window != nullptr)
    {
        windowing::present(*window);
    }
}
} // namespace ztermy::ui
