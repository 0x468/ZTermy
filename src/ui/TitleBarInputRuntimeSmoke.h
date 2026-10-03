#pragma once

#include <QQmlComponent>
#include <QScopeGuard>
#include <QWheelEvent>
#include "ui/RuntimeSmokeItems.h"

namespace ztermy::ui
{
// The strip's text and tool targets must not become accidental caption targets.
// Double-click is tested through delivered pointer events, not a direct signal.
inline bool verifySessionStripWindowControls(NativeWindow &window)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    auto *blank = quickItem(root, "terminalSessionStatusDragArea");
    auto *status = quickItem(root, "terminalSessionStatusText");
    auto *composer = quickItem(root, "terminalComposerAction");
    if (!blank || !status || !composer || blank->width() < 100 || blank->height() < 20)
        return false;
    const auto doubleClick = [&](QQuickItem &item) {
        // Qt's native input bridge synthesizes double-clicks from two presses;
        // passing MouseButtonDblClick into it directly asserts in Debug builds.
        sendMouseClick(window, item, {item.width() / 2, item.height() / 2});
        sendMouseClick(window, item, {item.width() / 2, item.height() / 2});
        processWindowEventsFor(200ms);
    };
    const auto original = window.geometry();
    sendMouseClick(window, *blank, {blank->width() / 2, blank->height() / 2});
    bool passed = window.geometry() == original && !root->property("titleBarShown").toBool();
    doubleClick(*status);
    passed = window.windowState() != Qt::WindowMaximized && window.geometry() == original && passed;
    doubleClick(*blank);
    passed = window.windowState() == Qt::WindowMaximized && passed;
    doubleClick(*blank);
    passed = window.windowState() != Qt::WindowMaximized && window.geometry() == original && passed;
    const bool originallyChecked = composer->property("checked").toBool();
    sendMouseClick(window, *composer, {composer->width() / 2, composer->height() / 2});
    passed = composer->property("checked").toBool() != originallyChecked && window.windowState() != Qt::WindowMaximized
             && passed;
    sendMouseClick(window, *composer, {composer->width() / 2, composer->height() / 2});
    processWindowEventsFor(400ms);
    passed = composer->property("checked").toBool() == originallyChecked && !root->property("titleBarShown").toBool()
             && passed;
    qInfo() << "Session strip: click inert, status excluded, double-click maximize/restore, tools preserved:" << passed;
    return passed;
}

inline bool reviewSessionStripNativeDrag(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    auto *blank = quickItem(root, "terminalSessionStatusDragArea");
    auto *viewport = terminalViewportItem(root);
    if (!blank || !viewport)
        return false;
    const auto originalGeometry = window.geometry();
    const auto originalSize = viewport->size();
    const auto originalOrigin = viewport->mapToScene({0, 0});
    const auto originalTabs = controller.terminalTabs().size();
    int resizeRequests = 0;
    const auto connection = QObject::connect(qobject_cast<TerminalItem *>(viewport), &TerminalItem::sizeRequested,
                                             &window, [&](auto, auto, auto, auto) {
                                                 ++resizeRequests;
                                             });
    const auto disconnect = qScopeGuard([&] {
        QObject::disconnect(connection);
    });
    const auto point = [&] {
        return window.mapToGlobal(blank->mapToScene({blank->width() / 2, blank->height() / 2}).toPoint());
    };
    qInfo() << "SESSION STRIP REVIEW: drag blank from" << point() << "window=" << window.geometry();
    if (!processWindowEventsUntil(
            [&] {
                return window.position() != originalGeometry.topLeft();
            },
            60s))
        return false;
    processWindowEventsFor(500ms);
    const bool movedCleanly = window.windowState() != Qt::WindowMaximized && viewport->size() == originalSize
                              && viewport->mapToScene({0, 0}) == originalOrigin && resizeRequests == 0
                              && !root->property("titleBarShown").toBool()
                              && controller.terminalTabs().size() == originalTabs;
    qInfo() << "SESSION STRIP MOVED: geometry=" << window.geometry() << "no grid/chrome/layout change=" << movedCleanly
            << "double-click at" << point();
    if (!movedCleanly
        || !processWindowEventsUntil(
            [&] {
                return window.windowState() == Qt::WindowMaximized;
            },
            60s))
        return false;
    processWindowEventsFor(300ms);
    qInfo() << "SESSION STRIP MAXIMIZED: double-click to restore at" << point();
    if (!processWindowEventsUntil(
            [&] {
                return window.windowState() != Qt::WindowMaximized;
            },
            60s))
        return false;
    processWindowEventsFor(400ms);
    qInfo() << "SESSION STRIP RESTORED:" << window.geometry();
    return !root->property("titleBarShown").toBool() && controller.terminalTabs().size() == originalTabs;
}

// Place a sensitive input surface behind the real chrome. Hidden chrome must
// allow these events; revealed and dismissing chrome must shield the same
// coordinates. This negative control catches visual-only overlays.
inline bool verifyTitleBarInputShield(NativeWindow &window)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    auto *bar = quickItem(root, "mainTitleBar");
    if (!bar)
        return false;
    QQmlComponent component(window.engine());
    component.setData(R"(
        import QtQuick
        import Ztermy
        MouseArea {
            width: 1100; height: 38; z: 89
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            property int presses: 0
            property int wheels: 0
            property bool motionActive: Motion.enabled && !Motion.reduced
            onPressed: presses++
            onWheel: wheel => { wheels++; wheel.accepted = true; }
        }
    )",
                      QUrl{});
    const std::unique_ptr<QObject> trap(component.create());
    auto *area = qobject_cast<QQuickItem *>(trap.get());
    if (!area)
    {
        qWarning() << component.errors();
        return false;
    }
    area->setParentItem(root);
    const auto probe = [&](const QPointF point) {
        const QPointF global = window.mapToGlobal(point.toPoint());
        qt_handleMouseEvent(&window, point, global, Qt::RightButton, Qt::RightButton, QEvent::MouseButtonPress, {},
                            static_cast<int>(GetTickCount()));
        qt_handleMouseEvent(&window, point, global, Qt::NoButton, Qt::RightButton, QEvent::MouseButtonRelease, {},
                            static_cast<int>(GetTickCount()));
        QWheelEvent wheel(point, global, {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &wheel);
    };
    QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
    root->setProperty("titleBarRevealed", false);
    processWindowEventsFor(250ms);
    probe({720, 20});
    QCursor::setPos(window.mapToGlobal(QPoint{43, 20}));
    processWindowEventsFor(50ms);
    const bool baseline = area->property("presses").toInt() == 1 && area->property("wheels").toInt() == 1
                          && area->property("containsMouse").toBool();
    QCursor::setPos(window.mapToGlobal(QPoint{500, 6}));
    root->setProperty("titleBarRevealed", true);
    processWindowEventsFor(40ms);
    const auto slidingWithoutFade = [&] {
        const auto progress = bar->property("revealProgress").toReal();
        const auto top = bar->mapToScene(QPointF{}).y();
        return progress > 0 && progress < 1 && top < 0 && top > -bar->height() && bar->opacity() == 1;
    };
    const bool entering = !area->property("motionActive").toBool() || slidingWithoutFade();
    const auto capture = [&](const QString &name) {
        const auto arguments = QCoreApplication::arguments();
        const auto data = arguments.indexOf(QStringLiteral("--data-dir"));
        return data >= 0 && data + 1 < arguments.size()
               && window.grabWindow().save(QDir(arguments[data + 1]).filePath(name));
    };
    const bool enterCaptured = capture(QStringLiteral("title-slide-enter.png"));
    processWindowEventsFor(210ms);
    const bool flushTop = qAbs(bar->mapToScene(QPointF{}).y()) < 0.01 && bar->opacity() == 1;
    probe({720, 20});
    // Also exercise the unused edge of the left navigation target.
    probe({1, 20});
    QCursor::setPos(window.mapToGlobal(QPoint{43, 20}));
    processWindowEventsFor(50ms);
    const bool blocked = area->property("presses").toInt() == 1 && area->property("wheels").toInt() == 1
                         && !area->property("containsMouse").toBool();
    QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
    root->setProperty("titleBarRevealed", false);
    processWindowEventsFor(40ms);
    const bool exiting = !area->property("motionActive").toBool() || slidingWithoutFade();
    if (area->property("motionActive").toBool())
        probe({720, 20});
    const bool dismissalBlocked = area->property("presses").toInt() == 1 && area->property("wheels").toInt() == 1;
    const bool exitCaptured = capture(QStringLiteral("title-slide-exit.png"));
    const bool hidden = processWindowEventsUntil(
        [&] {
            return !root->property("titleBarInteractive").toBool();
        },
        2s);
    probe({720, 20});
    const bool restored = area->property("presses").toInt() == 2 && area->property("wheels").toInt() == 2;
    QVariant held;
    QMetaObject::invokeMethod(root, "titleInteractionHeld", Q_RETURN_ARG(QVariant, held));
    qInfo() << "Slide completion state:" << hidden << root->property("titleBarShown") << bar->property("revealProgress")
            << held << area->property("presses") << area->property("wheels");
    qInfo() << "Chrome slide/shield: negative control, enter, flush top, blocked, exit, dismissal, restored:"
            << baseline << entering << flushTop << blocked << exiting << dismissalBlocked << restored;
    return baseline && entering && enterCaptured && flushTop && blocked && exiting && exitCaptured && dismissalBlocked
           && restored;
}

} // namespace ztermy::ui
