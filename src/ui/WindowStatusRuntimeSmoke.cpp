#include "ui/WindowStatusRuntimeSmoke.h"
#include "ui/WindowStateRuntimeSmoke.h"

namespace ztermy::ui
{
bool verifyTrayExit(NativeWindow &window)
{
    auto *controller = window.rootObject()->property("controller").value<QObject *>();
    if (!controller)
        return false;
    const auto originalTabs = controller->property("terminalTabs").toList();
    QStringList originalIds;
    for (const auto &tab : originalTabs)
        originalIds.append(tab.toMap().value(QStringLiteral("id")).toString());
    if (originalIds.size() != 3)
        return false;
    const auto arguments = QCoreApplication::arguments();
    const bool installerExit = arguments.contains(QStringLiteral("--installer-exit-smoke"));
    window.setCloseToTrayEnabled(!arguments.contains(QStringLiteral("--installer-exit-without-tray")));
    windowing::reveal(window);
    bool invoked = false;
    bool timedOut = false;
    const auto exitConnection =
        QObject::connect(&window, &NativeWindow::windowClosing, &window, [&](const bool quitApplication) {
            if (installerExit)
                invoked = quitApplication;
        });
    const auto disconnectExit = qScopeGuard([&] {
        QObject::disconnect(exitConnection);
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&] {
        timedOut = true;
        QCoreApplication::exit(EXIT_FAILURE);
    });
    QTimer trigger;
    trigger.setSingleShot(true);
    QObject::connect(&trigger, &QTimer::timeout, &window, [&] {
        bool detachedVisible = false;
        for (auto *candidate : QGuiApplication::allWindows())
            if (candidate->property("ownerWindowId").toString() == QStringLiteral("restore-window"))
                detachedVisible = candidate->isVisible();
        if (!detachedVisible || !window.isVisible())
        {
            QCoreApplication::exit(EXIT_FAILURE);
            return;
        }
        if (arguments.contains(QStringLiteral("--tray-exit-hidden")))
        {
            if (window.closeToTrayEnabled())
                window.close(); // Ordinary close-to-tray, followed by explicit exit.
            else
                window.hide();
            if (window.isVisible())
            {
                QCoreApplication::exit(EXIT_FAILURE);
                return;
            }
        }
        if (installerExit)
        {
            // The external runner posts the same registered message as zinstaller,
            // including to hidden windows. Do not invoke the exit method here.
            qInfo() << "Installer safe exit: receiver ready";
            return;
        }
        invoked = QMetaObject::invokeMethod(&window, "exitFromTray");
    });
    deadline.start(10000);
    trigger.start(1000);
    const int result = QCoreApplication::exec();
    deadline.stop();
    QStringList remainingIds;
    for (const auto &tab : controller->property("terminalTabs").toList())
        remainingIds.append(tab.toMap().value(QStringLiteral("id")).toString());
    const bool passed = invoked && !timedOut && result == EXIT_SUCCESS && remainingIds == originalIds;
    qInfo() << "Tray exit: event loop stopped without timeout and full Tab topology preserved:" << passed;
    return passed;
}

// Presentation-only fixture: protocol-fed status and routing have independent
// tests. Inspect real delegates in each window without starting remote sessions.
bool verifyWindowStatusPresentation(NativeWindow &main, QQuickWindow &detached)
{
    using namespace std::chrono_literals;
    auto *controller = main.rootObject()->property("controller").value<QObject *>();
    if (!controller)
        return false;
    bool passed = true;
    for (const auto &theme : {QStringLiteral("ztermy-light"), QStringLiteral("ztermy-dark")})
    {
        passed = QMetaObject::invokeMethod(controller, "previewTerminalTheme", Q_ARG(QString, theme)) && passed;
        processWindowEventsFor(300ms);
        for (const bool toDetached : {false, true})
        {
            QQuickWindow *target = toDetached ? &detached : &main;
            windowing::present(*target);
            processWindowEventsFor(300ms);
            const QString owner = toDetached ? QStringLiteral("detached") : QStringLiteral("main");
            const QString tabName = toDetached ? QStringLiteral("workspaceTitle-detached-selected")
                                               : QStringLiteral("workspaceTitle-main-check");
            auto *tab = findWindowSmokeItem(target->contentItem(), tabName);
            auto *toast = target->findChild<QObject *>(QStringLiteral("terminalNotificationToast"));
            if (!tab || !toast)
            {
                passed = false;
                continue;
            }
            for (const int state : {1, 2, 3, 4})
            {
                tab->setProperty("progressState", state);
                tab->setProperty("progressPercentage", 42);
                target->requestUpdate();
                processWindowEventsFor(150ms);
                passed = captureWindowSmokeItem(tab, QStringLiteral("status-%1-%2-%3.png").arg(theme, owner).arg(state))
                         && passed;
            }
            tab->setProperty("progressState", 0);
            auto *focused = QGuiApplication::focusWindow();
            const QVariantMap message{
                {QStringLiteral("windowId"), toDetached ? QStringLiteral("restore-window") : QStringLiteral("main")},
                {QStringLiteral("title"), QStringLiteral("任务完成 · Terminal notification")},
                {QStringLiteral("message"), QStringLiteral("UTF-8 中文内容 · <b>plain text, not markup</b>")}};
            passed = QMetaObject::invokeMethod(controller, "terminalNotificationRequested", Q_ARG(QVariantMap, message))
                     && passed;
            processWindowEventsFor(300ms);
            passed = toast->property("visible").toBool() && QGuiApplication::focusWindow() == focused && passed;
            passed = captureWindowSmokeItem(target->contentItem(),
                                            QStringLiteral("notification-%1-%2.png").arg(theme, owner))
                     && passed;
            QMetaObject::invokeMethod(toast, "close");
            processWindowEventsFor(200ms);
        }
    }
    QMetaObject::invokeMethod(controller, "endTerminalThemePreview");
    qInfo() << "Light/dark window status presentation and notification focus:" << passed;
    return passed;
}

bool captureNativeSnapFlyout(QQuickWindow &detached)
{
    using namespace std::chrono_literals;
    auto *button = findWindowSmokeItem(detached.contentItem(), QStringLiteral("detachedWindowAction-maximize"));
    const auto arguments = QCoreApplication::arguments();
    const auto dataIndex = arguments.indexOf(QStringLiteral("--data-dir"));
    if (!button || dataIndex < 0 || dataIndex + 1 >= arguments.size())
        return false;
    const auto handle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)
    windowing::present(detached);
    SetForegroundWindow(handle);
    if (!settleWindowUntil(
            [&] {
                return GetForegroundWindow() == handle;
            },
            2s))
        return false;
    POINT oldCursor{};
    if (!GetCursorPos(&oldCursor))
        return false;
    const auto restoreCursor = qScopeGuard([&] {
        SetCursorPos(oldCursor.x, oldCursor.y);
    });
    const QPointF center = button->mapToScene({button->width() / 2, button->height() / 2});
    POINT hover{.x = qRound(center.x() * detached.devicePixelRatio()),
                .y = qRound(center.y() * detached.devicePixelRatio())};
    if (!ClientToScreen(handle, &hover) || !SetCursorPos(hover.x, hover.y))
        return false;
    processWindowEventsFor(1800ms);
    auto *screen = detached.screen();
    const QRect area = screen->geometry();
    const int width = std::min(800, area.width());
    const int height = std::min(500, area.height());
    // Capture only the top-right of our maximized test window, including the
    // OS-owned flyout. A QML grab cannot contain another process's overlay.
    const auto capture = screen->grabWindow(0, area.width() - width, 0, width, height);
    const bool saved =
        !capture.isNull()
        && capture.save(QDir(arguments[dataIndex + 1]).filePath(QStringLiteral("native-snap-hover.png")));
    qInfo() << "Native Snap hover desktop capture saved; requires visual inspection:" << saved;
    return saved;
}

} // namespace ztermy::ui
