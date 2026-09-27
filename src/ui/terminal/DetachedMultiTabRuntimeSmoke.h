#pragma once

#include <QGuiApplication>
#include <QQuickItemGrabResult>
#include <QScreen>
#include <QStyleHints>
#include "ui/terminal/DetachedChromeRuntimeSmoke.h"
#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"
#include "ui/terminal/DetachedWindowActionsRuntimeSmoke.h"

namespace ztermy::ui
{
inline bool nativeTabDrag(QQuickWindow &source, const QPoint &start, const QPoint &end, const bool cancel = false,
                          QObject *dropObserver = nullptr)
{
    using namespace std::chrono_literals;
    const auto previous = QCursor::pos();
    windowing::present(source);
    QCursor::setPos(start);
    processWindowEventsFor(150ms);
    const auto handle = reinterpret_cast<HWND>(source.winId()); // NOLINT(performance-no-int-to-ptr)
    const auto mouse = [](const DWORD flags) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        return SendInput(1, &input, sizeof(input)) == 1;
    };
    POINT pointer{};
    bool pointerAvailable = GetCursorPos(&pointer) != FALSE;
    bool foreground = GetForegroundWindow() == handle;
    bool pointerTarget = pointerAvailable && GetAncestor(WindowFromPoint(pointer), GA_ROOT) == handle;
    if (!foreground && pointerTarget)
    {
        // Windows can deny programmatic activation of a background test. Only
        // click when the native hit is this owned window, then recheck both
        // prerequisites before sending a drag or keyboard cancellation.
        const bool pressed = mouse(MOUSEEVENTF_LEFTDOWN);
        const bool released = mouse(MOUSEEVENTF_LEFTUP);
        // The next press is a drag, not the second click of a tab rename.
        processWindowEventsFor(
            std::chrono::milliseconds(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50));
        pointerAvailable = GetCursorPos(&pointer) != FALSE;
        pointerTarget = pointerAvailable && GetAncestor(WindowFromPoint(pointer), GA_ROOT) == handle;
        foreground = pressed && released && GetForegroundWindow() == handle;
    }
    if (!foreground || !pointerTarget)
    {
        QCursor::setPos(previous);
        qWarning() << "Native tab drag unavailable: source is not the foreground pointer target"
                   << "foreground=" << foreground << "pointerAvailable=" << pointerAvailable
                   << "pointerTarget=" << pointerTarget << "requestedLogical=" << start
                   << "actualNative=" << QPoint(pointer.x, pointer.y) << "sourceGeometry=" << source.geometry();
        return false;
    }
    const bool pressed = mouse(MOUSEEVENTF_LEFTDOWN);
    processWindowEventsFor(50ms);
    for (int step = 1; step <= 20; ++step)
    {
        QCursor::setPos(start + (end - start) * (static_cast<double>(step) / 20));
        processWindowEventsFor(30ms);
    }
    bool canceled = true;
    if (cancel)
    {
        INPUT key{};
        key.type = INPUT_KEYBOARD;
        key.ki.wVk = VK_ESCAPE;
        canceled = SendInput(1, &key, sizeof(key)) == 1;
        key.ki.dwFlags = KEYEVENTF_KEYUP;
        canceled = (SendInput(1, &key, sizeof(key)) == 1) && canceled;
        processWindowEventsFor(100ms);
    }
    if (dropObserver)
        qInfo() << "Native drag before release target:" << dropObserver->property("dropTarget").toMap();
    const bool released = mouse(MOUSEEVENTF_LEFTUP);
    QCursor::setPos(previous);
    processWindowEventsFor(400ms);
    return pressed && released && canceled;
}

inline bool verifyDetachedCrossWindowTabs(AppController &controller, QQuickWindow &source, const QString &tabId,
                                          const QString &otherId)
{
    using namespace std::chrono_literals;
    const auto originalGeometry = source.geometry();
    const auto owner = source.property("ownerWindowId").toString();
    if (!controller.detachTerminalWorkspace(otherId))
        return false;
    processWindowEventsFor(400ms);
    QPointer<QQuickWindow> target;
    const auto targetOwner = controller.terminalWorkspace(otherId).value(QStringLiteral("windowId")).toString();
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == targetOwner)
            target = qobject_cast<QQuickWindow *>(candidate);
    if (!target)
        return false;
    const auto area = source.screen()->availableGeometry();
    source.setGeometry({area.x() + 10, area.y() + 50, 480, 400});
    target->setGeometry({area.x() + 510, area.y() + 50, 480, 400});
    windowing::present(*target);
    processWindowEventsFor(250ms);
    const auto drag = [&](QQuickWindow &from, QQuickWindow &to, const bool cancel) {
        auto *tab = detachedVisualQuickItem(from.contentItem(), QStringLiteral("workspaceTitle-") + tabId);
        return tab
               && nativeTabDrag(from, tab->mapToGlobal({60, 16}).toPoint(), to.mapToGlobal(QPoint{160, 16}), cancel);
    };
    const bool canceled = drag(source, *target, true)
                          && controller.terminalWorkspace(tabId).value(QStringLiteral("windowId")).toString() == owner
                          && !target->property("tabBarPreview").toBool() && !target->property("tabBarVisible").toBool();
    const bool transferred =
        drag(source, *target, false)
        && controller.terminalWorkspace(tabId).value(QStringLiteral("windowId")).toString() == targetOwner
        && target->property("tabBarVisible").toBool() && !target->property("tabBarPreview").toBool();
    const bool returned = transferred && drag(*target, source, false)
                          && controller.terminalWorkspace(tabId).value(QStringLiteral("windowId")).toString() == owner;
    const bool restored = controller.insertTerminalWorkspace(otherId, 0, QStringLiteral("main"));
    source.setGeometry(originalGeometry);
    processWindowEventsFor(400ms);
    qInfo() << "Detached native tab drag: Escape preserves owner, cross-window, return:" << canceled << transferred
            << returned;
    return canceled && transferred && returned && restored;
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
    const auto resolve = [&] {
        QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
        return coordinator->property("dropTarget").toMap();
    };
    const auto uncovered = resolve();
    const bool exactPane =
        uncovered.value(QStringLiteral("paneId")).toString() == paneId
        && uncovered.value(QStringLiteral("windowId")).toString() == detached.property("ownerWindowId").toString();
    QWindow blocker;
    blocker.setFlags(Qt::Tool | Qt::FramelessWindowHint);
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

inline bool verifyNativeExactPaneDrop(NativeWindow &mainWindow, AppController &controller, QQuickWindow &source,
                                      const QString &mainId)
{
    using namespace std::chrono_literals;
    const auto mainGeometry = mainWindow.geometry();
    const auto sourceGeometry = source.geometry();
    const auto owner = source.property("ownerWindowId").toString();
    if (!controller.activateTerminalTab(mainId))
        return false;
    const auto targetPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (!controller.splitActiveTerminal(QStringLiteral("horizontal"), true))
        return false;
    const auto otherPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    const auto movingId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto movingPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (movingId.isEmpty() || !controller.insertTerminalWorkspace(movingId, 0, owner))
        return false;
    mainWindow.rootObject()->setProperty("requestedMainWorkspaceId", mainId);
    mainWindow.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    QMetaObject::invokeMethod(mainWindow.rootObject(), "refreshMainWorkspace");
    const auto area = source.screen()->availableGeometry();
    mainWindow.setGeometry({area.x() + 10, area.y() + 50, 500, 500});
    source.setGeometry({area.x() + 530, area.y() + 50, 480, 500});
    windowing::present(mainWindow);
    processWindowEventsFor(400ms);
    auto *tab = detachedVisualQuickItem(source.contentItem(), QStringLiteral("workspaceTitle-") + movingId);
    auto *view = detachedVisualQuickItem(mainWindow.contentItem(), QStringLiteral("terminalViewport-") + targetPane);
    auto *coordinator = mainWindow.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    if (view && coordinator)
    {
        const auto point = view->mapToGlobal({view->width() * 0.1, view->height() * 0.5});
        QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
        qInfo() << "Exact pane preflight target:" << coordinator->property("dropTarget").toMap() << "point=" << point
                << "viewport=" << view->size();
        coordinator->setProperty("dropTarget", QVariantMap{});
    }
    const bool dragged =
        tab && view
        && nativeTabDrag(source, tab->mapToGlobal({60, 16}).toPoint(),
                         view->mapToGlobal({view->width() * 0.1, view->height() * 0.5}).toPoint(), false, coordinator);
    processWindowEventsFor(400ms);
    const auto workspace = controller.terminalWorkspace(mainId);
    const auto root = workspace.value(QStringLiteral("root")).toMap();
    const auto first = root.value(QStringLiteral("first")).toMap();
    const bool exact =
        dragged && controller.terminalWorkspace(movingId).isEmpty()
        && workspace.value(QStringLiteral("paneCount")).toInt() == 3
        && first.value(QStringLiteral("first")).toMap().value(QStringLiteral("id")).toString() == movingPane
        && first.value(QStringLiteral("second")).toMap().value(QStringLiteral("id")).toString() == targetPane
        && root.value(QStringLiteral("second")).toMap().value(QStringLiteral("id")).toString() == otherPane;
    qInfo() << "Exact pane drop details: dragged, source removed, pane count, expected target/moved/sibling:" << dragged
            << controller.terminalWorkspace(movingId).isEmpty() << workspace.value(QStringLiteral("paneCount"))
            << targetPane << movingPane << otherPane;
    qInfo() << "Exact pane drop actual first/first-first/first-second/second:" << first.value(QStringLiteral("id"))
            << first.value(QStringLiteral("first")).toMap().value(QStringLiteral("id"))
            << first.value(QStringLiteral("second")).toMap().value(QStringLiteral("id"))
            << root.value(QStringLiteral("second")).toMap().value(QStringLiteral("id"));
    if (!controller.terminalWorkspace(movingId).isEmpty())
        controller.closeTerminalTab(movingId);
    else if (controller.activateTerminalPane(movingPane))
        controller.closeActiveTerminalPane();
    if (controller.activateTerminalPane(otherPane))
        controller.closeActiveTerminalPane();
    mainWindow.setGeometry(mainGeometry);
    source.setGeometry(sourceGeometry);
    processWindowEventsFor(400ms);
    qInfo() << "Native cross-window drop targets non-active pane, keeps sibling and pane identity:" << exact;
    return exact;
}

inline bool verifyDetachedTabGrouping(NativeWindow &window, AppController &controller, const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto settle = [&window] {
        window.requestUpdate();
        processWindowEventsFor(400ms);
    };
    const auto a = controller.activeTerminalTabId();
    const auto pane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (!controller.detachTerminalWorkspace(a))
        return false;
    settle();
    const auto owner = controller.terminalWorkspace(a).value(QStringLiteral("windowId")).toString();
    const auto b = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto main = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (b.isEmpty() || main.isEmpty() || !controller.insertTerminalWorkspace(b, 1, owner))
        return false;
    settle();
    QPointer<QQuickWindow> detached;
    int count = 0;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->objectName() == QStringLiteral("detachedTerminalWindow") && candidate->isVisible())
        {
            ++count;
            detached = qobject_cast<QQuickWindow *>(candidate);
        }
    if (count != 1 || !detached || detached->property("workspaceId").toString() != b)
        return false;
    if (detached->property("tabBarVisible").toBool() || !QMetaObject::invokeMethod(detached, "toggleTabBar"))
        return false;
    settle();
    if (QCoreApplication::arguments().contains(QStringLiteral("--detached-chrome-only")))
        return verifyDetachedCreationMenu(window, controller, *detached, outputDirectory)
               && verifyDetachedLogicalTabMerge(window, controller, *detached, a, b)
               && verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory);
    auto *tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "activated"))
        return false;
    settle();
    const bool selected =
        detached->property("workspaceId").toString() == a
        && detachedVisualQuickItem(detached->contentItem(), QStringLiteral("terminalViewport-") + pane);
    controller.activateTerminalTab(main);
    settle();
    const bool independent = detached->property("workspaceId").toString() == a;
    const bool dropOcclusion = verifyDetachedDropOcclusion(window, *detached, pane);
    tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab)
        return false;
    const auto tabStart = tab->mapToScene({60, 16});
    const bool pointerDrag =
        nativeTabDrag(*detached, detached->mapToGlobal(tabStart.toPoint()), detached->mapToGlobal(QPoint{319, 16}));
    settle();
    QStringList reordered;
    for (const auto &entry : controller.terminalTabs())
        if (entry.toMap().value(QStringLiteral("windowId")).toString() == owner)
            reordered.append(entry.toMap().value(QStringLiteral("id")).toString());
    const bool dragged = pointerDrag && reordered == QStringList{b, a};
    qInfo() << "Detached tab pointer drag reorders:" << dragged;
    if (!dragged)
        return false;
    if (!verifyDetachedCrossWindowTabs(controller, *detached, b, main))
        return false;
    if (!verifyNativeExactPaneDrop(window, controller, *detached, main))
        return false;
    if (!verifyDetachedPaneZoom(window, controller, *detached, a, b))
        return false;
    if (!verifyDetachedSearch(window, controller, *detached, a, main))
        return false;
    if (!verifyDetachedSmartSplit(controller, *detached))
        return false;
    if (!verifyDetachedCreationMenu(window, controller, *detached, outputDirectory))
        return false;
    if (!verifyDetachedInactiveTabMerge(window, controller, *detached, a, b,
                                        [](QQuickWindow &source, const QPoint &start, const QPoint &end) {
                                            return nativeTabDrag(source, start, end);
                                        }))
        return false;
    tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "renameRequested"))
        return false;
    settle();
    auto *dialog = detached->findChild<QObject *>(QStringLiteral("terminalRenameDialog"));
    auto *field = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("renameTerminalTitleField"));
    const bool localDialog = dialog && dialog->property("visible").toBool() && field && field->window() == detached;
    if (!localDialog)
        return false;
    field->setProperty("text", QStringLiteral("Detached renamed"));
    QMetaObject::invokeMethod(field, "accepted");
    settle();
    const bool renamed = !dialog->property("visible").toBool()
                         && controller.terminalWorkspace(a).value(QStringLiteral("title")).toString()
                                == QStringLiteral("Detached renamed");
    tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "duplicateRequested"))
        return false;
    settle();
    const auto duplicate = controller.activeTerminalTabId();
    const bool duplicated =
        duplicate != a && duplicate != b && duplicate != main
        && controller.terminalWorkspace(duplicate).value(QStringLiteral("windowId")).toString() == owner;
    if (!duplicated)
        return false;
    QMetaObject::invokeMethod(detached, "closeTab", Q_ARG(QVariant, duplicate));
    settle();
    // The preceding round trip leaves a, b; duplication inserts between them.
    const bool successor = controller.activeTerminalTabId() == b && detached->property("workspaceId").toString() == b
                           && window.rootObject()->property("mainWorkspaceId").toString() == main;
    qInfo() << "Detached close keeps displayed and active successor together:" << successor;
    qInfo() << "Detached close actual active/displayed/main, expected successor/main:"
            << controller.activeTerminalTabId() << detached->property("workspaceId")
            << window.rootObject()->property("mainWorkspaceId") << b << main;
    if (!successor)
        return false;
    QMetaObject::invokeMethod(detached, "selectWorkspace", Q_ARG(QVariant, a));
    settle();
    const bool captionRoundTrip = verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory);
    settle();
    const auto capture = detached->contentItem()->grabToImage();
    const bool captured =
        capture
        && settleWindowUntil(
            [&] {
                return !capture->image().isNull();
            },
            3s)
        && capture->image().save(QDir(outputDirectory).filePath(QStringLiteral("detached-multi-tab.png")));
    if (QCoreApplication::arguments().contains(QStringLiteral("--reattach-all-only")))
    {
        const auto layoutA = controller.terminalWorkspace(a).value(QStringLiteral("root"));
        const auto layoutB = controller.terminalWorkspace(b).value(QStringLiteral("root"));
        auto *button = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("detachedReattachAllButton"));
        if (!button || !QMetaObject::invokeMethod(button, "activated"))
            return false;
        settle();
        const bool returned =
            detached.isNull()
            && controller.terminalWorkspace(a).value(QStringLiteral("windowId")).toString() == QStringLiteral("main")
            && controller.terminalWorkspace(b).value(QStringLiteral("windowId")).toString() == QStringLiteral("main")
            && controller.terminalWorkspace(a).value(QStringLiteral("root")) == layoutA
            && controller.terminalWorkspace(b).value(QStringLiteral("root")) == layoutB
            && window.rootObject()->property("mainWorkspaceId").toString() == a;
        qInfo() << "Explicit reattach all: both tabs, unchanged pane trees, selected tab and closed empty window:"
                << returned;
        return selected && independent && captured && returned && captionRoundTrip;
    }
    QMetaObject::invokeMethod(detached, "closeTab", Q_ARG(QVariant, a));
    settle();
    const bool keptWindow = detached && detached->isVisible() && detached->property("workspaceId").toString() == b
                            && controller.terminalWorkspace(a).isEmpty();
    if (detached)
        detached->close();
    settle();
    const bool closed =
        detached.isNull() && controller.terminalWorkspace(b).isEmpty() && !controller.terminalWorkspace(main).isEmpty();
    qInfo() << "Detached multi-tab: selected, independent, captured, same window after close, closed group:" << selected
            << independent << captured << keptWindow << closed;
    qInfo() << "Detached local rename and duplicate:" << localDialog << renamed << duplicated;
    return selected && independent && captured && keptWindow && closed && localDialog && renamed && duplicated
           && dropOcclusion && captionRoundTrip;
}
} // namespace ztermy::ui
