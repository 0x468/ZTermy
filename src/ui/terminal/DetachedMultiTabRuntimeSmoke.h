#pragma once

#include <QGuiApplication>
#include <QQuickItemGrabResult>
#include <QScreen>
#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"

namespace ztermy::ui
{
inline bool nativeTabDrag(QQuickWindow &source, const QPoint &start, const QPoint &end, const bool cancel = false)
{
    using namespace std::chrono_literals;
    const auto previous = QCursor::pos();
    windowing::present(source);
    QCursor::setPos(start);
    processWindowEventsFor(150ms);
    const auto handle = reinterpret_cast<HWND>(source.winId()); // NOLINT(performance-no-int-to-ptr)
    POINT pointer{};
    if (GetForegroundWindow() != handle || !GetCursorPos(&pointer)
        || GetAncestor(WindowFromPoint(pointer), GA_ROOT) != handle)
    {
        QCursor::setPos(previous);
        qWarning() << "Native tab drag unavailable: source is not the foreground pointer target";
        return false;
    }
    const auto mouse = [](const DWORD flags) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        return SendInput(1, &input, sizeof(input)) == 1;
    };
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
               && nativeTabDrag(from, tab->mapToGlobal({60, 16}).toPoint(), to.mapToGlobal(QPoint{300, 16}), cancel);
    };
    const bool canceled = drag(source, *target, true)
                          && controller.terminalWorkspace(tabId).value(QStringLiteral("windowId")).toString() == owner;
    const bool transferred =
        drag(source, *target, false)
        && controller.terminalWorkspace(tabId).value(QStringLiteral("windowId")).toString() == targetOwner;
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
    coordinator->setProperty("movingWindow", QVariant::fromValue(static_cast<QObject *>(&blocker)));
    const bool ignoresMovingSource = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("movingWindow", QVariant::fromValue(static_cast<QObject *>(nullptr)));
    blocker.hide();
    processWindowEventsFor(100ms);
    const bool visibleAgain = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("dropTarget", QVariantMap{});
    qInfo() << "Detached drop: exact pane, obscured, ignore moving source, visible again:" << exactPane << obscured
            << ignoresMovingSource << visibleAgain;
    return exactPane && obscured && ignoresMovingSource && visibleAgain;
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
        nativeTabDrag(*detached, detached->mapToGlobal(tabStart.toPoint()), detached->mapToGlobal(QPoint{300, 16}));
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
