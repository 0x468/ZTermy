#include "core/windowing/WindowPresenter.h"

#include "core/windowing/WindowStateTransitions.h"

#include <QWindow>
#include <algorithm>

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

QRect boundedRestoreGeometry(const QRect normalGeometry, const QRect availableGeometry, const QSize minimumSize)
{
    if (availableGeometry.isEmpty())
        return normalGeometry;
    const int width = std::clamp(normalGeometry.width(), std::clamp(minimumSize.width(), 1, availableGeometry.width()),
                                 availableGeometry.width());
    const int height =
        std::clamp(normalGeometry.height(), std::clamp(minimumSize.height(), 1, availableGeometry.height()),
                   availableGeometry.height());
    return {std::clamp(normalGeometry.x(), availableGeometry.left(), availableGeometry.right() - width + 1),
            std::clamp(normalGeometry.y(), availableGeometry.top(), availableGeometry.bottom() - height + 1), width,
            height};
}

void restorePlacement(QWindow &window, const QRect normalGeometry, const bool maximized)
{
    // Callers restore hidden windows. Clear stale flags before assigning normal
    // bounds so Windows does not treat those bounds as a maximized resize.
    window.setWindowStates(Qt::WindowNoState);
    window.setGeometry(normalGeometry);
    window.setWindowStates(maximized ? Qt::WindowMaximized : Qt::WindowNoState);
}
} // namespace ztermy::windowing
