#pragma once

#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"

#include <QTimer>

namespace ztermy::ui
{

inline bool verifyDetachedSharedTint(NativeWindow &window, AppController &controller, QQuickWindow &detached,
                                     const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    const auto oldTheme = controller.terminalThemeId();
    // Detaching the last workspace intentionally sends Main back to Hosts.
    // Compare terminal surfaces, not a terminal with the opaque Hosts page.
    const auto mainFixture = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (mainFixture.isEmpty())
        return false;
    QMetaObject::invokeMethod(root, "activateMainTerminal", Q_ARG(QVariant, mainFixture));
    processWindowEventsFor(600ms);
    const auto restoreAppearance = qScopeGuard([&] {
        controller.saveTerminalTheme(oldTheme);
        controller.closeTerminalTab(mainFixture);
        root->setProperty("appearancePreviewActive", false);
        QMetaObject::invokeMethod(root, "applyWindowAppearance");
    });
    bool passed = true;
    for (const auto *palette : {"ztermy-dark", "ztermy-light"})
        for (const auto *material : {"transparent", "acrylic", "solid"})
        {
            passed = controller.saveTerminalTheme(QString::fromLatin1(palette)) && passed;
            root->setProperty("previewBackdropPreference", QString::fromLatin1(material));
            root->setProperty("previewBackdropOpacity", 0.45);
            root->setProperty("appearancePreviewActive", true);
            QMetaObject::invokeMethod(root, "applyWindowAppearance");
            processWindowEventsFor(450ms);
            auto *tint = detachedVisualQuickItem(detached.contentItem(), QStringLiteral("detachedWorkspaceBackground"));
            const auto expected = root->property("backgroundColor").value<QColor>();
            const QColor tintColour = tint ? tint->property("color").value<QColor>() : QColor{};
            const QImage mainScene = window.grabWindow();
            const QImage detachedScene = detached.grabWindow();
            if (mainScene.isNull() || detachedScene.isNull())
                return false;
            const QColor mainPixel = mainScene.pixelColor(mainScene.width() * 3 / 10, mainScene.height() * 4 / 5);
            const QColor detachedPixel =
                detachedScene.pixelColor(detachedScene.width() * 3 / 10, detachedScene.height() * 4 / 5);
            const auto matches = [](QColor left, QColor right) {
                return qAbs(left.red() - right.red()) <= 2 && qAbs(left.green() - right.green()) <= 2
                       && qAbs(left.blue() - right.blue()) <= 2 && qAbs(left.alpha() - right.alpha()) <= 1;
            };
            const bool sameLayer = tintColour == expected && matches(mainPixel, detachedPixel)
                                   && qAbs(detachedPixel.alpha() - expected.alpha()) <= 1;
            const auto name =
                QStringLiteral("shared-tint-%1-%2").arg(QLatin1StringView{palette}, QLatin1StringView{material});
            const bool captured =
                mainScene.save(QDir(outputDirectory).filePath(name + QStringLiteral("-main.png")))
                && detachedScene.save(QDir(outputDirectory).filePath(name + QStringLiteral("-detached.png")));
            qInfo() << "Shared workspace tint:" << name << mainPixel << detachedPixel
                    << "same single layer=" << sameLayer;
            passed = sameLayer && captured && passed;
        }
    const auto arguments = QCoreApplication::arguments();
    const bool briefReview = arguments.contains(QStringLiteral("--detached-material-desktop-review-brief"));
    if (briefReview || arguments.contains(QStringLiteral("--detached-material-desktop-review")))
    {
        const auto originalMainGeometry = window.geometry();
        const auto originalDetachedGeometry = detached.geometry();
        QQuickWindow backing;
        backing.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        backing.setGeometry(80, 120, 1360, 500);
        backing.setColor(QColor(QStringLiteral("#505050")));
        backing.show();
        const bool oldRequest = root->property("windowAlwaysOnTopRequested").toBool();
        const bool oldTopmost = window.alwaysOnTop();
        root->setProperty("windowAlwaysOnTopRequested", true);
        window.setAlwaysOnTop(true);
        const auto handle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)
        const bool wasTopmost = (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        const auto restoreZOrder = qScopeGuard([&] {
            root->setProperty("windowAlwaysOnTopRequested", oldRequest);
            window.setAlwaysOnTop(oldTopmost);
            if (!wasTopmost)
                SetWindowPos(handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            window.setGeometry(originalMainGeometry);
            detached.setGeometry(originalDetachedGeometry);
            processWindowEventsFor(300ms);
        });
        controller.saveTerminalTheme(QStringLiteral("ztermy-light"));
        root->setProperty("previewBackdropPreference", QStringLiteral("acrylic"));
        QMetaObject::invokeMethod(root, "applyWindowAppearance");
        window.setGeometry(80, 120, 660, 500);
        detached.setGeometry(780, 120, 660, 500);
        window.hide();
        window.show();
        windowing::present(window);
        windowing::present(detached);
        SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        QTimer captureTimer;
        HWND previous = nullptr;
        QObject::connect(&captureTimer, &QTimer::timeout, &window, [&] {
            const auto foreground = GetForegroundWindow();
            const auto mainHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
            const bool mainActive = window.isActive();
            const bool detachedActive = detached.isActive();
            if (foreground == previous || mainActive == detachedActive
                || (foreground != mainHandle && foreground != handle))
                return;
            previous = foreground;
            const auto name =
                mainActive ? QStringLiteral("native-main-active.png") : QStringLiteral("native-detached-active.png");
            const bool saved =
                window.screen()->grabWindow(0, 80, 120, 1360, 500).save(QDir(outputDirectory).filePath(name));
            qInfo() << "Actual material activation capture:" << name << "main/detached active=" << mainActive
                    << detachedActive << saved;
        });
        captureTimer.start(500);
        qInfo() << "Detached material desktop review: click either window to compare activation";
        processWindowEventsFor(briefReview ? 1s : 120s);
    }
    return passed;
}

inline bool verifyDetachedDropOcclusion(NativeWindow &mainWindow, QQuickWindow &detached, const QString &paneId)
{
    using namespace std::chrono_literals;
    auto *coordinator = mainWindow.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    auto *view = detachedVisualQuickItem(detached.contentItem(), QStringLiteral("terminalViewport-") + paneId);
    if (!coordinator || !view)
        return false;
    windowing::present(detached);
    processWindowEventsFor(200ms);
    const auto point = view->mapToGlobal({view->width() * 0.15, view->height() * 0.5});
    const auto previousPointer = QCursor::pos();
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(previousPointer);
    });
    QCursor::setPos(point.toPoint());
    processWindowEventsFor(900ms);
    const auto resolve = [&] {
        QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
        return coordinator->property("dropTarget").toMap();
    };
    const auto uncovered = resolve();
    qInfo() << "Detached uncovered probe:" << point << detached.geometry() << detached.isVisible()
            << detached.windowState() << "leaf=" << view->size() << view->mapToScene({0, 0}) << uncovered;
    const bool exactPane =
        uncovered.value(QStringLiteral("paneId")).toString() == paneId
        && uncovered.value(QStringLiteral("windowId")).toString() == detached.property("ownerWindowId").toString();
    QWindow blocker;
    blocker.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    blocker.setGeometry(detached.geometry());
    windowing::present(blocker);
    processWindowEventsFor(200ms);
    const bool obscured = resolve().isEmpty();
    const bool ignoresMovingSource =
        windowing::isUnobscuredDropTarget(detached, detached.mapFromGlobal(point.toPoint()), &blocker);
    blocker.hide();
    processWindowEventsFor(100ms);
    const bool visibleAgain = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("dropTarget", QVariantMap{});
    qInfo() << "Detached drop: exact pane, obscured, ignore moving source, visible again:" << exactPane << obscured
            << ignoresMovingSource << visibleAgain;
    return exactPane && obscured && ignoresMovingSource && visibleAgain;
}

inline bool verifyDetachedSingleWorkspace(NativeWindow &window, AppController &controller,
                                          const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto id = controller.activeTerminalTabId();
    const auto pane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (!controller.detachTerminalWorkspace(id))
        return false;
    const auto owner = controller.terminalWorkspace(id).value(QStringLiteral("windowId")).toString();
    processWindowEventsFor(500ms);
    QPointer<QQuickWindow> detached;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == owner)
            detached = qobject_cast<QQuickWindow *>(candidate);
    if (!detached)
        return false;
    auto *view = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("detachedWorkspaceViewport"));
    if (!view || view->y() != 0 || detached->property("windowControlsVisible").toBool()
        || detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + id))
        return false;
    const auto size = view->size();
    bool passed = verifyDetachedSharedTint(window, controller, *detached, outputDirectory);
    if (!QMetaObject::invokeMethod(detached, "toggleWindowControls"))
        return false;
    processWindowEventsFor(300ms);
    passed = view->size() == size && view->y() == 0 && verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory)
             && passed;
    const auto tabCount = controller.terminalTabs().size();
    const int paneCount = controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt();
    const bool created = QMetaObject::invokeMethod(detached, "openLocalTab", Q_ARG(QVariant, QString{}),
                                                   Q_ARG(QVariant, QStringLiteral("commandPrompt")));
    processWindowEventsFor(400ms);
    passed = created && controller.terminalTabs().size() == tabCount
             && controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt() == paneCount + 1 && passed;
    const auto createdPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    passed = controller.activateTerminalPane(createdPane) && controller.closeActiveTerminalPane() && passed;
    processWindowEventsFor(300ms);
    // Keep this owned target away from the host app's topmost side panel.
    // It must be exposed before testing whether another window blocks drops.
    detached->setGeometry(100, 100, 940, 660);
    detached->hide();
    detached->show();
    windowing::present(*detached);
    processWindowEventsFor(300ms);
    const auto detachedHandle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
    // Background launches cannot steal foreground activation. Expose only this
    // owned fixture above unrelated desktop windows; the blocker below is also
    // topmost so the occlusion rejection remains an actual negative check.
    const auto restoreZOrder = qScopeGuard([&] {
        if (detached)
            SetWindowPos(detachedHandle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    SetWindowPos(detachedHandle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    passed = verifyDetachedDropOcclusion(window, *detached, pane) && passed;
    const auto incoming = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto incomingPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *coordinator = window.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    auto *leaf = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("terminalViewport-") + pane);
    if (!coordinator || !leaf || incoming.isEmpty())
        return false;
    windowing::present(*detached);
    processWindowEventsFor(300ms);
    const auto point = leaf->mapToGlobal({leaf->width() * 0.1, leaf->height() * 0.5});
    QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
    const auto target = coordinator->property("dropTarget").toMap();
    passed = target.value(QStringLiteral("mode")).toString() == QStringLiteral("merge")
             && target.value(QStringLiteral("paneId")).toString() == pane && passed;
    QMetaObject::invokeMethod(coordinator, "finishTabDrop", Q_ARG(QVariant, incoming), Q_ARG(QVariant, false));
    processWindowEventsFor(500ms);
    passed = controller.terminalWorkspace(incoming).isEmpty()
             && controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt() == paneCount + 1
             && detached->property("tabs").toList().size() == 1 && passed;
    qInfo() << "Detached: no Tabs, overlay controls, new Pane, incoming merge:" << passed;
    if (!controller.activateTerminalPane(incomingPane) || !controller.closeActiveTerminalPane())
        return false;
    processWindowEventsFor(250ms);
    detached->setProperty("windowControlsVisible", true);
    const auto layout = controller.terminalWorkspace(id).value(QStringLiteral("root"));
    auto *returnButton =
        detachedVisualQuickItem(detached->contentItem(), QStringLiteral("terminalPaneAction-detach-") + pane);
    passed =
        detached->grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-single-workspace.png")))
        && returnButton
        && returnButton->property("label").toString()
               == QCoreApplication::translate("DetachedTerminalWindow", "Reattach window to main window")
        && passed;
    const bool clicked = returnButton && QMetaObject::invokeMethod(returnButton, "clicked");
    processWindowEventsFor(400ms);
    const auto returned = controller.terminalWorkspace(id);
    const bool reattached =
        clicked && detached.isNull() && returned.value(QStringLiteral("windowId")) == QStringLiteral("main")
        && returned.value(QStringLiteral("root")) == layout && controller.terminalTabs().size() == tabCount
        && window.rootObject()->property("mainWorkspaceId") == id;
    qInfo() << "Pane toolbar returns whole window without creating another detached owner:" << reattached;
    passed = reattached && passed;
    qInfo() << "Detached single workspace lifecycle passed=" << passed;
    return passed;
}
} // namespace ztermy::ui
