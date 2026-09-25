#include "ui/WindowControl.h"

#include "core/windowing/WindowPresenter.h"
#include "platform/windows/DetachedWindowNativeFrame.h"

namespace ztermy::ui
{
WindowControl::WindowControl(QObject *parent) : QObject(parent) {}

bool WindowControl::acceptsDropAt(QWindow *window, const QPointF localPosition, QWindow *movingWindow) const
{
    return window && windowing::isUnobscuredDropTarget(*window, localPosition, movingWindow);
}

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
