#pragma once

#include "ui/RuntimeSmokeItems.h"

#include <QScopeGuard>

#include <array>
#include <cmath>
#include <utility>

namespace ztermy::ui
{
// Exercise the real HWND hit test and native mouse delivery at physical y=0/1.
// Only the fixture's owned, foreground window is allowed to receive the click.
inline bool verifyTitleTabTopEdge(NativeWindow &window, AppController &controller, const QStringList &ids)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    auto *bar = quickItem(root, "mainTitleBar");
    const auto arguments = QCoreApplication::arguments();
    const auto dataIndex = arguments.indexOf(QStringLiteral("--data-dir"));
    if (!root || !bar || ids.size() != 3 || dataIndex < 0 || dataIndex + 1 >= arguments.size())
        return false;
    QDir captures(QDir(arguments[dataIndex + 1]).filePath(QStringLiteral("top-edge-captures")));
    if (!captures.mkpath(QStringLiteral(".")))
        return false;
    const auto handle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    const auto originalState = window.windowStates();
    const auto originalGeometry = window.geometry();
    const auto restore = qScopeGuard([&] {
        window.setWindowStates(originalState);
        window.setGeometry(originalGeometry);
    });
    const auto nativeHit = [&](const POINT point) {
        return SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };
    const auto topPoint = [&](const QQuickItem &tab, const int position, const int row) {
        const auto origin = tab.mapToScene(QPointF{});
        const auto scale = window.devicePixelRatio();
        const int left = static_cast<int>(std::ceil(origin.x() * scale));
        const int right = static_cast<int>(std::ceil((origin.x() + tab.width()) * scale)) - 1;
        POINT point{.x = position == 0 ? left : position == 1 ? (left + right) / 2 : right, .y = row};
        ClientToScreen(handle, &point);
        return point;
    };
    const auto activate = [&](const QString &id, const POINT point) {
        const auto hit = nativeHit(point);
        if (controller.activeTerminalTabId() == id || GetForegroundWindow() != handle
            || GetAncestor(WindowFromPoint(point), GA_ROOT) != handle || hit != HTCLIENT)
        {
            qWarning() << "Native top-edge preconditions: already active, foreground, owns point, hit:"
                       << (controller.activeTerminalTabId() == id) << (GetForegroundWindow() == handle)
                       << (GetAncestor(WindowFromPoint(point), GA_ROOT) == handle) << hit << point.x << point.y;
            return false;
        }
        if (!SetCursorPos(point.x, point.y))
            return false;
        std::array<INPUT, 2> inputs{};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), static_cast<int>(sizeof(INPUT)))
            != inputs.size())
            return false;
        return processWindowEventsUntil(
            [&] {
                return controller.activeTerminalTabId() == id;
            },
            2s);
    };
    const auto settleTabs = [&] {
        QList<QRectF> previous;
        int stableSamples = 0;
        return processWindowEventsUntil(
            [&] {
                QList<QRectF> current;
                for (const auto &id : ids)
                {
                    auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + id));
                    if (!tab)
                        return false;
                    current.push_back(QRectF(tab->mapToScene(QPointF{}), tab->size()));
                }
                stableSamples = current == previous ? stableSamples + 1 : 0;
                previous = std::move(current);
                return stableSamples >= 5;
            },
            3s);
    };
    for (const auto state : {Qt::WindowMaximized, Qt::WindowFullScreen})
        for (const auto *theme : {"ztermy-dark", "ztermy-light"})
            for (const bool automatic : {false, true})
                for (const auto *widthMode : {"active", "equal"})
                {
                    qInfo() << "Top-edge case:" << state << theme << automatic << widthMode;
                    window.setWindowStates(state);
                    window.requestActivate();
                    if (!controller.saveTerminalTheme(QString::fromLatin1(theme))
                        || !controller.saveWindowInteractionSettings(
                            {{QStringLiteral("autoHideTitleBar"), automatic},
                             {QStringLiteral("tabWidthMode"), QString::fromLatin1(widthMode)}}))
                        return false;
                    controller.activateTerminalTab(ids.back());
                    QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
                    if (automatic)
                    {
                        auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + ids.front()));
                        if (!tab)
                            return false;
                        if (!processWindowEventsUntil(
                                [&] {
                                    return !root->property("titleBarInteractive").toBool()
                                           && nativeHit(topPoint(*tab, 1, 0)) == HTCAPTION;
                                },
                                2s))
                        {
                            qWarning() << "Hidden strip not ready:" << root->property("titleBarInteractive")
                                       << bar->property("revealProgress") << nativeHit(topPoint(*tab, 1, 0));
                            return false;
                        }
                        const auto point = topPoint(*tab, 1, 0);
                        if (!SetCursorPos(point.x, point.y))
                            return false;
                    }
                    if (!processWindowEventsUntil(
                            [&] {
                                return bar->property("revealProgress").toReal() == 1 && window.isActive();
                            },
                            3s))
                        return false;
                    // Width-policy/theme changes can queue native appearance work
                    // before the relocate animation starts. Wait for actual stable
                    // delegate geometry, not a fixed wall-clock delay.
                    if (!settleTabs())
                        return false;
                    auto *strip = quickItem(root, "titleTerminalTabs");
                    if (!strip || !QMetaObject::invokeMethod(strip, "positionViewAtBeginning") || !settleTabs())
                        return false;
                    auto *viewport = terminalViewportItem(root);
                    if (!viewport)
                        return false;
                    const auto viewportSize = viewport->size();
                    const auto viewportOrigin = viewport->mapToScene(QPointF{});
                    for (int probe = 0; probe < 6; ++probe)
                    {
                        const auto &id = ids[probe % 3];
                        auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + id));
                        auto *background = quickItem(tab, "tabSelectedBackground");
                        if (!tab || !background || !tab->property("topEdgeHitEnabled").toBool() || background->y() != 4
                            || background->height() != tab->height() - 4
                            || !activate(id, topPoint(*tab, probe % 3, probe / 3)))
                        {
                            qWarning() << "Top-edge tab switch failed:" << state << theme << automatic << widthMode
                                       << probe << window.windowStates() << window.visibility()
                                       << (tab ? tab->property("topEdgeHitEnabled") : QVariant{})
                                       << (background ? background->y() : -1)
                                       << (background ? background->height() : -1);
                            window.grabWindow().save(captures.filePath(QStringLiteral("failed-case.png")));
                            return false;
                        }
                        if (!settleTabs())
                            return false;
                        if (!root->property("titleBarShown").toBool() || viewport->size() != viewportSize
                            || viewport->mapToScene(QPointF{}) != viewportOrigin
                            || controller.terminalTabs().size() != 3)
                            return false;
                    }
                    const auto name = QStringLiteral("%1-%2-%3-%4.png")
                                          .arg(state == Qt::WindowMaximized ? QStringLiteral("maximized")
                                                                            : QStringLiteral("fullscreen"),
                                               QString::fromLatin1(theme),
                                               automatic ? QStringLiteral("overlay") : QStringLiteral("persistent"),
                                               QString::fromLatin1(widthMode));
                    if (!window.grabWindow().save(captures.filePath(name)))
                        return false;
                    qInfo() << "Physical top pixels and unchanged visual inset / viewport:" << name
                            << window.devicePixelRatio();
                }
    // Restore mode must keep its resize border; the close affordance must still win.
    window.setWindowStates(Qt::WindowMaximized);
    if (!controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), false},
                                                   {QStringLiteral("tabCloseButton"), QStringLiteral("always")}}))
        return false;
    processWindowEventsFor(300ms);
    auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + ids.front()));
    if (!tab)
        return false;
    sendMouseClick(window, *tab, {tab->width() - 16, tab->height() / 2});
    if (!processWindowEventsUntil(
            [&] {
                return controller.terminalTabs().size() == 2;
            },
            2s))
        return false;
    for (const auto &entry : controller.terminalTabs())
        if (entry.toMap().value(QStringLiteral("id")).toString() == ids.front())
            return false;
    window.setWindowStates(Qt::WindowNoState);
    window.setGeometry(originalGeometry);
    processWindowEventsFor(300ms);
    tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + ids.back()));
    if (!tab || tab->property("topEdgeHitEnabled").toBool() || nativeHit(topPoint(*tab, 1, 0)) != HTTOP)
        return false;
    qInfo() << "Close action and restored-window resize border preserved";
    return true;
}
} // namespace ztermy::ui
