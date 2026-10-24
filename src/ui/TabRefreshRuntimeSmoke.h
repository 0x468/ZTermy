#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QPointer>
#include <QScopeGuard>

namespace ztermy::ui
{
inline bool verifyTabRefreshClicks(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    for (const auto &tab : controller.terminalTabs())
        controller.closeTerminalTab(tab.toMap().value(QStringLiteral("id")).toString());
    const QString targetId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const QString busyId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (targetId.isEmpty() || busyId.isEmpty())
        return false;
    auto *root = window.rootObject();
    root->setProperty("currentPage", QStringLiteral("terminal"));
    const bool resetControl = QCoreApplication::arguments().contains(QStringLiteral("--tab-refresh-reset-control"));
    auto *titleView = quickItem(root, "titleTerminalTabs");
    if (!titleView)
        return false;
    QMetaObject::Connection resetConnection;
    if (resetControl)
    {
        const auto replaceList = [&controller, titleView] {
            titleView->setProperty("model", controller.terminalTabs());
        };
        replaceList();
        resetConnection = QObject::connect(&controller, &AppController::terminalTabsChanged, titleView, replaceList);
    }
    const auto disconnectControl = qScopeGuard([&] {
        QObject::disconnect(resetConnection);
    });
    showForRuntimeSmoke(window);
    window.showMaximized();
    window.requestActivate();
    const auto previousPointer = QCursor::pos();
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(previousPointer);
    });
    const auto handle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    int update = 0;
    for (const auto state : {QWindow::Maximized, QWindow::FullScreen, QWindow::Windowed})
    {
        window.setVisibility(state);
        for (const auto *mode : {"equal", "active"})
        {
            if (!controller.saveWindowInteractionSettings({{QStringLiteral("tabWidthMode"), QString::fromLatin1(mode)}})
                || !controller.activateTerminalTab(busyId))
                return false;
            processWindowEventsFor(350ms);
            auto *target = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + targetId));
            if (!target)
            {
                qWarning() << "Refresh fixture could not locate its target delegate";
                return false;
            }
            QPointer<QQuickItem> original = target;
            auto *view = quickItem(root, "titleTerminalTabs");
            qInfo() << "Refresh fixture view count, width, desired, available, navigation" << view->property("count")
                    << view->width() << view->property("desiredTabWidth") << view->property("availableWidth")
                    << root->property("titleNavigationWidth");
            const QPointF local = target->mapToScene(QPointF{target->width() / 2, target->height() / 2});
            POINT point{.x = qRound(local.x() * window.devicePixelRatio()),
                        .y = qRound(local.y() * window.devicePixelRatio())};
            ClientToScreen(handle, &point);
            SetCursorPos(point.x, point.y);
            if (SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) != HTCLIENT)
            {
                qWarning() << "Refresh fixture target is not native client input";
                return false;
            }
            // Deliver through Qt's platform input queue to this owned window;
            // foreground changes in the developer's desktop cannot redirect input.
            const QPointF global = window.mapToGlobal(local.toPoint());
            qt_handleMouseEvent(&window, local, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress, {},
                                static_cast<int>(GetTickCount()));
            bool buttonDown = true;
            const auto releaseMouse = qScopeGuard([&] {
                if (buttonDown)
                {
                    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::LeftButton,
                                        QEvent::MouseButtonRelease, {}, static_cast<int>(GetTickCount()));
                }
            });
            processWindowEventsFor(20ms);
            const bool pressed = target->property("pressed").toBool();
            bool hitsStable = true;
            for (int frame = 0; frame < 5; ++frame)
            {
                if (!controller.setTerminalTabTitle(busyId, QStringLiteral("refresh-%1").arg(++update)))
                    return false;
                processWindowEventsFor(20ms);
                hitsStable =
                    hitsStable && SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTCLIENT;
            }
            qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease, {},
                                static_cast<int>(GetTickCount()));
            buttonDown = false;
            processWindowEventsFor(150ms);
            const bool activated = controller.activeTerminalTabId() == targetId;
            const bool sameItem =
                original && original == quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + targetId));
            qInfo() << "Refresh during native click: state, mode, stable delegate, hit, activated" << state << mode
                    << sameItem << hitsStable << activated << "pressed=" << pressed
                    << "target size=" << (original ? original->size() : QSizeF{});
            if (resetControl)
                return pressed && !sameItem && hitsStable && !activated;
            if (!pressed || !sameItem || !hitsStable || !activated)
                return false;
        }
    }
    return true;
}
} // namespace ztermy::ui
