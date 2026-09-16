#include "core/windowing/WindowPresenter.h"

#include "core/windowing/WindowStateTransitions.h"

#include <QWindow>

namespace ztermy::windowing
{
namespace
{
void applyStates(QWindow &window, const Qt::WindowStates states)
{
    if (window.windowStates() != states)
    {
        window.setWindowStates(states);
    }
    // QWindow::show() would reset the states to the platform default; making the
    // window visible directly keeps the flags that were just applied.
    window.setVisible(true);
}
} // namespace

void minimize(QWindow &window)
{
    applyStates(window, minimizedStates(window.windowStates()));
}

void toggleMaximize(QWindow &window)
{
    applyStates(window, maximizeToggledStates(window.windowStates()));
}

void reveal(QWindow &window)
{
    applyStates(window, window.windowStates());
}

void present(QWindow &window)
{
    applyStates(window, presentedStates(window.windowStates()));
    window.raise();
    window.requestActivate();
}
} // namespace ztermy::windowing
