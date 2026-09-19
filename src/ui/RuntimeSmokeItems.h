#pragma once

#include "ui/terminal/TerminalPaneRuntimeSmoke.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QScreen>
#include <QStringView>

#include <algorithm>
#include <chrono>
#include <vector>

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow *window, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                                    const QString &text = {}, bool autorepeat = false, ushort count = 1);
QT_END_NAMESPACE

namespace ztermy::ui
{
[[nodiscard]] inline QQuickItem *quickItem(QQuickItem *rootObject, const char *objectName)
{
    if (rootObject == nullptr)
        return nullptr;
    const QString name = QString::fromLatin1(objectName);
    if (auto *item = rootObject->findChild<QQuickItem *>(name))
        return item;
    // Repeater delegates can belong to a different QObject parent tree.
    std::vector<QQuickItem *> pending{rootObject};
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        auto *item = pending[index];
        if (item->objectName() == name)
            return item;
        const auto children = item->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return nullptr;
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

// Hover-sensitive checks need the physical pointer outside the window: Qt re-delivers
// hover at the real pointer position on every frame, which would undo synthetic moves.
inline void moveWindowAwayFromPointer(ztermy::NativeWindow &window)
{
    const QPoint pointer = QCursor::pos();
    if (!window.frameGeometry().contains(pointer) || window.screen() == nullptr)
        return;
    const QRect available = window.screen()->availableGeometry();
    const bool pointerOnLeft = pointer.x() < available.center().x();
    window.setX(pointerOnLeft ? std::max(available.left(), available.right() - window.frameGeometry().width())
                              : available.left());
    processWindowEventsFor(std::chrono::milliseconds{100});
}

inline void sendMouseMove(ztermy::NativeWindow &window, QQuickItem &item, const QPointF itemPosition)
{
    const QPointF local = item.mapToScene(itemPosition);
    const QPointF global = window.mapToGlobal(local.toPoint());
    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, {},
                        static_cast<int>(GetTickCount()));
    processWindowEventsFor(std::chrono::milliseconds{250});
}

template <typename Predicate>
[[nodiscard]] bool processWindowEventsUntil(Predicate predicate, const std::chrono::milliseconds timeout)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout.count())
    {
        if (predicate())
        {
            return true;
        }
        processWindowEventsFor(std::chrono::milliseconds{25});
    }
    return predicate();
}

inline void sendKey(ztermy::NativeWindow &window, const Qt::Key key, const Qt::KeyboardModifiers modifiers = {})
{
    qt_handleKeyEvent(&window, QEvent::KeyPress, key, modifiers);
    QCoreApplication::processEvents();
    qt_handleKeyEvent(&window, QEvent::KeyRelease, key, modifiers);
    processWindowEventsFor(std::chrono::milliseconds{40});
}

inline void sendText(ztermy::NativeWindow &window, const QStringView text)
{
    for (const QChar character : text)
    {
        qt_handleKeyEvent(&window, QEvent::KeyPress, Qt::Key_unknown, {}, QString{character});
        QCoreApplication::processEvents();
        qt_handleKeyEvent(&window, QEvent::KeyRelease, Qt::Key_unknown, {}, QString{character});
    }
    processWindowEventsFor(std::chrono::milliseconds{40});
}

// Moves keyboard focus to a named item and confirms the named focus chain lands on it.
[[nodiscard]] inline bool focusItem(ztermy::NativeWindow &window, QQuickItem *item, const QString &expectedName)
{
    if (item == nullptr || !item->isVisible() || !item->isEnabled())
    {
        qWarning() << "UI keyboard smoke focus target unavailable" << expectedName;
        return false;
    }
    item->forceActiveFocus(Qt::TabFocusReason);
    processWindowEventsFor(std::chrono::milliseconds{40});
    const QString actualName = namedFocusItem(window);
    if (actualName != expectedName)
    {
        qWarning() << "UI keyboard smoke focus mismatch" << "expected=" << expectedName << "actual=" << actualName;
        return false;
    }
    return true;
}
template <typename Range>
[[nodiscard]] inline bool verifySettingsFocusOrder(NativeWindow &window, QQuickItem *root, const Range &order)
{
    if (!focusItem(window, quickItem(root, order.front()), QString::fromLatin1(order.front())))
        return false;
    for (std::size_t index = 1; index < order.size(); ++index)
    {
        sendKey(window, Qt::Key_Tab);
        // A non-default row inserts its accessible reset button before its editor.
        if (namedFocusItem(window).startsWith(QStringLiteral("settingsRowReset-")))
            sendKey(window, Qt::Key_Tab);
        const QString expected = QString::fromLatin1(order[index]);
        if (const QString actual = namedFocusItem(window); actual != expected)
        {
            qWarning() << "Settings Tab order mismatch" << "index=" << index << "expected=" << expected
                       << "actual=" << actual;
            return false;
        }
    }
    return true;
}
} // namespace ztermy::ui
