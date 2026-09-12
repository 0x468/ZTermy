#pragma once

#include "ui/terminal/TerminalPaneRuntimeSmoke.h"

#include <vector>

namespace ztermy::ui
{
[[nodiscard]] inline QQuickItem *quickItem(QQuickItem *rootObject, const char *objectName)
{
    return rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QString::fromLatin1(objectName));
}

[[nodiscard]] inline QQuickItem *visualQuickItem(QQuickItem *rootObject, const char *objectName)
{
    if (rootObject == nullptr)
    {
        return nullptr;
    }
    const QString expectedName = QString::fromLatin1(objectName);
    std::vector<QQuickItem *> pending{rootObject};
    QQuickItem *fallback = nullptr;
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        if (candidate->objectName() == expectedName)
        {
            fallback = fallback == nullptr ? candidate : fallback;
            if (candidate->isVisible())
            {
                return candidate;
            }
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return fallback;
}

[[nodiscard]] inline QString namedFocusItem(const ztermy::NativeWindow &window)
{
    for (QQuickItem *item = window.activeFocusItem(); item != nullptr; item = item->parentItem())
    {
        if (!item->objectName().isEmpty())
        {
            return item->objectName();
        }
    }
    return {};
}

[[nodiscard]] inline bool terminalViewportHasFocus(const ztermy::NativeWindow &window)
{
    const QString focusName = namedFocusItem(window);
    return focusName == QStringLiteral("terminalViewport") || focusName.startsWith(QStringLiteral("terminalViewport-"));
}

[[nodiscard]] inline QQuickItem *terminalViewportItem(QQuickItem *rootObject)
{
    if (rootObject == nullptr)
    {
        return nullptr;
    }
    std::vector<QQuickItem *> pending{rootObject};
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        const QString name = candidate->objectName();
        if (name == QStringLiteral("terminalViewport") || name.startsWith(QStringLiteral("terminalViewport-")))
        {
            return candidate;
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return nullptr;
}

inline void sendMouseClick(ztermy::NativeWindow &window, QQuickItem &item, const QPointF itemPosition)
{
    const int timestamp = static_cast<int>(GetTickCount());
    const QPointF local = item.mapToScene(itemPosition);
    const QPointF global = window.mapToGlobal(local.toPoint());
    qt_handleMouseEvent(&window, local, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress, {},
                        timestamp);
    QCoreApplication::processEvents();
    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease, {},
                        static_cast<int>(GetTickCount()));
    processWindowEventsFor(std::chrono::milliseconds{80});
}

inline void sendMouseMove(ztermy::NativeWindow &window, QQuickItem &item, const QPointF itemPosition)
{
    const QPointF local = item.mapToScene(itemPosition);
    const QPointF global = window.mapToGlobal(local.toPoint());
    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, {},
                        static_cast<int>(GetTickCount()));
    processWindowEventsFor(std::chrono::milliseconds{250});
}
} // namespace ztermy::ui
