#pragma once

#include "application/AppController.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalItem.h"

#include <algorithm>
#include <vector>

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers modifiers, int timestamp);
QT_END_NAMESPACE

namespace ztermy::ui
{
// Synthesizes one mouse event in window scene coordinates; delivery is synchronous.
inline void synthesizeMouse(QQuickWindow &window, const QPointF &point, const Qt::MouseButtons buttons,
                            const Qt::MouseButton button, const QEvent::Type type)
{
    qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type, Qt::NoModifier,
                        static_cast<int>(GetTickCount()));
}
inline void sendMouse(QQuickWindow &window, const QPointF &point, const Qt::MouseButtons buttons,
                      const Qt::MouseButton button, const QEvent::Type type,
                      const std::chrono::milliseconds settle = std::chrono::milliseconds{30})
{
    synthesizeMouse(window, point, buttons, button, type);
    processWindowEventsFor(settle);
}

// A real pointer hovers before it presses (Main.qml's drag capture layer only arms
// while a header is hovered). Press and release go back to back: a loop spin between
// them can deliver a native WM_MOUSELEAVE that clears MouseArea's hover, and a
// release without hover is not a click.
inline void clickMouse(QQuickWindow &window, const QPointF &point)
{
    sendMouse(window, point, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
    synthesizeMouse(window, point, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    sendMouse(window, point, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
}

inline void dragMouse(QQuickWindow &window, const QPointF &start, const QPointF &end, const int steps,
                      const std::chrono::milliseconds settle = std::chrono::milliseconds{30})
{
    sendMouse(window, start, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, settle);
    sendMouse(window, start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress, settle);
    for (int step = 1; step <= steps; ++step)
        sendMouse(window, start + (end - start) * (static_cast<double>(step) / steps), Qt::LeftButton, Qt::NoButton,
                  QEvent::MouseMove, settle);
    sendMouse(window, end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease, settle);
}

inline bool verifyInactivePaneSelection(NativeWindow &window, AppController &controller, const QString &firstId,
                                        const QString &secondId, TerminalItem *targetPane)
{
    const auto settle = [] {
        processWindowEventsFor(std::chrono::milliseconds{250});
    };
    controller.activateTerminalPane(firstId);
    if (auto *sourcePane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + firstId))
        sourcePane->forceActiveFocus(Qt::OtherFocusReason);
    settle();
    int activationResizes = 0;
    std::vector<terminal::TerminalSelectionGestureType> selectionEvents;
    const auto resizeConnection = QObject::connect(targetPane, &TerminalItem::sizeRequested, targetPane, [&] {
        ++activationResizes;
    });
    const auto gestureConnection =
        QObject::connect(targetPane, &TerminalItem::selectionGestureRequested, targetPane, [&](const auto &gesture) {
            selectionEvents.push_back(gesture.type);
        });
    const QPointF selectionStart = targetPane->mapToScene(QPointF(16, 18));
    const QPointF selectionEnd = targetPane->mapToScene(QPointF(std::min<qreal>(180, targetPane->width() - 16), 18));
    dragMouse(window, selectionStart, selectionEnd, 20, std::chrono::milliseconds{40});
    settle();
    QObject::disconnect(resizeConnection);
    QObject::disconnect(gestureConnection);
    const bool continuousSelection =
        activationResizes == 0 && !selectionEvents.empty()
        && selectionEvents.back() == terminal::TerminalSelectionGestureType::release
        && std::ranges::find(selectionEvents, terminal::TerminalSelectionGestureType::cancel) == selectionEvents.end();
    qInfo() << "Inactive pane drag continuity:" << continuousSelection << "resize notifications=" << activationResizes;
    const bool selectionPaneActive =
        controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() == secondId;
    const bool inactivePaneSelectionPreserved = continuousSelection && selectionPaneActive && targetPane->hasSelection()
                                                && targetPane->selectionActionVisible();
    qInfo() << "Inactive pane activates without interrupting selection:" << inactivePaneSelectionPreserved
            << "active=" << selectionPaneActive << "selected=" << targetPane->hasSelection()
            << "actions=" << targetPane->selectionActionVisible();
    return inactivePaneSelectionPreserved;
}
inline bool verifyNestedPaneEdges(NativeWindow &window, AppController &controller, const QString &outputDirectory)
{
    const QString previous = controller.activeTerminalTabId();
    const QString workspace = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (workspace.isEmpty())
        return false;
    const auto activePane = [&] {
        return controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    };
    QStringList panes{activePane()};
    bool passed = true;
    for (const auto &orientation :
         {QStringLiteral("vertical"), QStringLiteral("horizontal"), QStringLiteral("horizontal")})
    {
        passed = controller.splitActiveTerminal(orientation, true) && passed;
        panes.push_back(activePane());
    }
    window.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    for (int frame = 0; frame < 6; ++frame)
    {
        processWindowEventsFor(std::chrono::milliseconds{70});
        static_cast<void>(window.grabWindow());
    }
    for (const auto &id : panes)
    {
        auto *viewport = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + id);
        auto *edges = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneFocusEdges-") + id);
        if (!viewport || !edges)
        {
            passed = false;
            continue;
        }
        const auto position = viewport->mapToScene(QPointF{});
        passed = passed && edges->property("thickness").toInt() == 1 && qAbs(position.x() - qRound(position.x())) < 0.01
                 && qAbs(position.y() - qRound(position.y())) < 0.01;
    }
    passed = window.grabWindow().save(outputDirectory + QStringLiteral("/four-pane-edges.png")) && passed;
    controller.closeTerminalTab(workspace);
    controller.activateTerminalTab(previous);
    qInfo() << "Four-pane edges have equal thickness and integral origins:" << passed;
    return passed;
}
} // namespace ztermy::ui
