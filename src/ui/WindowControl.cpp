#include "ui/WindowControl.h"

#include <QGuiApplication>
#include <QScreen>
#include "core/windowing/WindowPresenter.h"
#include "platform/windows/DetachedWindowNativeFrame.h"

namespace ztermy::ui
{
WindowControl::WindowControl(QObject *parent) : QObject(parent) {}

QVariantMap WindowControl::placement(QWindow *window, const QVariantMap &previous) const
{
    if (!window)
        return {};
    auto result = previous;
    const auto states = window->windowStates();
    if (!(states & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen)))
    {
        const auto normal = window->geometry();
        result.insert(QStringLiteral("x"), normal.x());
        result.insert(QStringLiteral("y"), normal.y());
        result.insert(QStringLiteral("width"), normal.width());
        result.insert(QStringLiteral("height"), normal.height());
        if (window->screen())
            result.insert(QStringLiteral("screenName"), window->screen()->name());
    }
    // Never replace normal placement with a maximized/minimized rectangle.
    if (result.value(QStringLiteral("width")).toInt() <= 0 || result.value(QStringLiteral("height")).toInt() <= 0)
        return {};
    result.insert(QStringLiteral("maximized"), states.testFlag(Qt::WindowMaximized));
    return result;
}

QVariantMap WindowControl::restorePlacement(QWindow *window, const QVariantMap &saved) const
{
    if (!window || saved.isEmpty())
        return placement(window, {});
    const QRect normal(saved.value(QStringLiteral("x")).toInt(), saved.value(QStringLiteral("y")).toInt(),
                       saved.value(QStringLiteral("width")).toInt(), saved.value(QStringLiteral("height")).toInt());
    if (normal.isEmpty())
        return placement(window, {});
    QScreen *screen = QGuiApplication::primaryScreen();
    qint64 bestOverlap = 0;
    for (auto *candidate : QGuiApplication::screens())
    {
        if (candidate->name() == saved.value(QStringLiteral("screenName")).toString())
        {
            screen = candidate;
            break;
        }
        const auto overlap = candidate->availableGeometry().intersected(normal);
        const qint64 area = static_cast<qint64>(overlap.width()) * overlap.height();
        if (!overlap.isEmpty() && area > bestOverlap)
        {
            bestOverlap = area;
            screen = candidate;
        }
    }
    if (!screen)
        return {};
    const auto fitted = windowing::boundedRestoreGeometry(normal, screen->availableGeometry(), window->minimumSize());
    auto result = saved;
    result.insert(QStringLiteral("x"), fitted.x());
    result.insert(QStringLiteral("y"), fitted.y());
    result.insert(QStringLiteral("width"), fitted.width());
    result.insert(QStringLiteral("height"), fitted.height());
    result.insert(QStringLiteral("screenName"), screen->name());
    window->setScreen(screen);
    windowing::restorePlacement(*window, fitted, saved.value(QStringLiteral("maximized")).toBool());
    return result;
}

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
