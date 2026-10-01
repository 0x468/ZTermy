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
            property bool motionActive: Motion.enabled
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
    const bool entering = !area->property("motionActive").toBool() || (bar->opacity() > 0 && bar->opacity() < 1);
    processWindowEventsFor(210ms);
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
    const bool exiting = !area->property("motionActive").toBool() || (bar->opacity() > 0 && bar->opacity() < 1);
    if (area->property("motionActive").toBool())
        probe({720, 20});
    const bool dismissalBlocked = area->property("presses").toInt() == 1 && area->property("wheels").toInt() == 1;
    processWindowEventsFor(200ms);
    probe({720, 20});
    const bool restored = area->property("presses").toInt() == 2 && area->property("wheels").toInt() == 2;
    qInfo() << "Chrome shield: negative control, enter, blocked, exit, dismissal, restored:" << baseline << entering
            << blocked << exiting << dismissalBlocked << restored;
    return baseline && entering && blocked && exiting && dismissalBlocked && restored;
}

// Detect hover/focus regressions through real cursor position and live delegates.
// Only isolated local fixture sessions are created; no command is typed.
inline bool verifyImmersiveTitleBar(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    const auto args = QCoreApplication::arguments();
    const auto data = args.indexOf(QStringLiteral("--data-dir"));
    if (data < 0 || data + 1 >= args.size())
        return false;
    // Repeated CTest runs reuse their isolated store. Do not let a previous
    // fixture's saved Tabs contaminate the three-Tab width/focus experiment.
    const auto previousTabs = controller.terminalTabs();
    for (const auto &tab : previousTabs)
        controller.closeTerminalTab(tab.toMap().value(QStringLiteral("id")).toString());
    const auto originalPointer = QCursor::pos();
    auto *root = window.rootObject();
    const bool originallyTopmost = window.alwaysOnTop();
    const bool originallyRequested = root->property("windowAlwaysOnTopRequested").toBool();
    root->setProperty("windowAlwaysOnTopRequested", true);
    window.setAlwaysOnTop(true);
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(originalPointer);
        root->setProperty("windowAlwaysOnTopRequested", originallyRequested);
        window.setAlwaysOnTop(originallyTopmost);
    });
    window.setGeometry(100, 100, 1120, 740);
    window.show();
    // The automated launcher uses SW_HIDE; cycle Qt visibility so the first
    // native ShowWindow call cannot leave an invisible pointer target.
    window.hide();
    window.show();
    window.requestActivate();
    if (!controller.saveWindowInteractionSettings(
            {{QStringLiteral("autoHideTitleBar"), true}, {QStringLiteral("tabWidthMode"), QStringLiteral("equal")}}))
        return false;
    QStringList ids;
    for (int index = 0; index < 3; ++index)
    {
        const auto id = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
        if (id.isEmpty())
            return false;
        ids.push_back(id);
    }
    root->setProperty("currentPage", QStringLiteral("terminal"));
    QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
    processWindowEventsFor(800ms);
    auto *viewport = terminalViewportItem(root);
    if (!viewport || root->property("titleBarShown").toBool())
        return false;
    // The hosts navigation column animates away when entering a terminal.
    // Establish a settled baseline before attributing a resize to hover.
    int stableSamples = 0;
    for (int sample = 0; sample < 30 && stableSamples < 5; ++sample)
    {
        const auto previousSize = viewport->size();
        const auto previousOrigin = viewport->mapToScene({0, 0});
        processWindowEventsFor(100ms);
        stableSamples =
            viewport->size() == previousSize && viewport->mapToScene({0, 0}) == previousOrigin ? stableSamples + 1 : 0;
    }
    if (stableSamples < 5)
        return false;
    if (!verifySessionStripWindowControls(window))
        return false;
    if (args.contains(QStringLiteral("--session-strip-desktop-review")))
        return reviewSessionStripNativeDrag(window, controller);
    const auto size = viewport->size();
    const auto origin = viewport->mapToScene({0, 0});
    int resizeRequests = 0;
    const auto connection = QObject::connect(qobject_cast<TerminalItem *>(viewport), &TerminalItem::sizeRequested,
                                             &window, [&](auto, auto, auto, auto) {
                                                 ++resizeRequests;
                                             });
    const auto disconnect = qScopeGuard([&] {
        QObject::disconnect(connection);
    });
    const int trigger = root->property("titleTriggerHeight").toInt();
    QCursor::setPos(window.mapToGlobal(QPoint{500, trigger - 2}));
    processWindowEventsFor(450ms);
    qInfo() << "Strip pointer probe:" << QCursor::pos() << window.geometry() << window.isActive()
            << window.titleBarPointerInside(trigger) << root->property("titleBarShown")
            << root->property("titleHoverElapsed") << "viewport=" << viewport->size() << size
            << "origin=" << viewport->mapToScene({0, 0}) << origin << "resize=" << resizeRequests;
    bool passed = root->property("titleBarShown").toBool() && viewport->size() == size
                  && viewport->mapToScene({0, 0}) == origin && resizeRequests == 0;
    qInfo() << "Immersive strip reveal / unchanged geometry and grid:" << passed;
    for (const auto &id : ids)
    {
        auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + id));
        if (!tab)
            return false;
        const auto point = tab->mapToScene({tab->width() / 2, 18});
        QCursor::setPos(window.mapToGlobal(point.toPoint()));
        sendMouseClick(window, *tab, {tab->width() / 2, 18});
        processWindowEventsFor(600ms);
        const bool switched = root->property("titleBarShown").toBool() && controller.activeTerminalTabId() == id;
        qInfo() << "Pointer-held Tab switch:" << switched;
        passed = switched && passed;
    }
    qInfo() << "Immersive remains open across pointer-held Tab switches:" << passed;
    QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
    processWindowEventsFor(700ms);
    const bool hiddenAfterLeave = !root->property("titleBarShown").toBool();
    passed = hiddenAfterLeave && passed;
    QCursor::setPos(window.mapToGlobal(QPoint{500, 20}));
    processWindowEventsFor(500ms);
    const bool terminalHoverInert = !root->property("titleBarShown").toBool();
    passed = terminalHoverInert && passed;
    qInfo() << "Terminal hover does not reveal / pointer leave hides:" << terminalHoverInert << hiddenAfterLeave;
    passed = verifyTitleBarInputShield(window) && passed;
    QDir captures(QDir(args[data + 1]).filePath(QStringLiteral("captures")));
    if (!captures.mkpath(QStringLiteral(".")))
        return false;
    for (const auto *palette : {"ztermy-dark", "ztermy-light", "solarized-light", "catppuccin-mocha"})
    {
        passed = controller.saveTerminalTheme(QString::fromLatin1(palette)) && passed;
        for (const bool automatic : {false, true})
        {
            passed =
                controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), automatic}}) && passed;
            QCursor::setPos(window.mapToGlobal(QPoint{500, trigger - 2}));
            processWindowEventsFor(650ms);
            const auto name = QStringLiteral("%1-%2.png")
                                  .arg(QLatin1StringView{palette},
                                       automatic ? QStringLiteral("overlay") : QStringLiteral("persistent"));
            passed = window.grabWindow().save(captures.filePath(name)) && passed;
        }
    }
    for (const auto *palette : {"ztermy-dark", "ztermy-light"})
        for (const auto *material : {"transparent", "acrylic", "solid"})
            for (const bool automatic : {false, true})
            {
                passed = controller.saveTerminalTheme(QString::fromLatin1(palette)) && passed;
                passed = controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), automatic}})
                         && passed;
                root->setProperty("previewBackdropPreference", QString::fromLatin1(material));
                root->setProperty("previewBackdropOpacity", 0.0);
                root->setProperty("appearancePreviewActive", true);
                QMetaObject::invokeMethod(root, "applyWindowAppearance");
                QCursor::setPos(window.mapToGlobal(QPoint{500, trigger - 2}));
                processWindowEventsFor(650ms);
                const auto scene = window.grabWindow();
                if (scene.isNull())
                    return false;
                const auto inkBackground =
                    scene.pixelColor(qRound(720 * scene.devicePixelRatio()), qRound(20 * scene.devicePixelRatio()));
                const bool surfaceValid = automatic || QLatin1StringView{material} == "solid"
                                              ? inkBackground.alpha() == 255
                                              : inkBackground.alpha() == 0;
                passed = surfaceValid && passed;
                const auto name = QStringLiteral("%1-%2-%3-zero")
                                      .arg(QLatin1StringView{palette}, QLatin1StringView{material},
                                           automatic ? QStringLiteral("overlay") : QStringLiteral("persistent"));
                const auto screenOrigin = window.mapToGlobal(QPoint{}) - window.screen()->geometry().topLeft();
                const auto desktop =
                    window.screen()->grabWindow(0, screenOrigin.x(), screenOrigin.y(), window.width(), window.height());
                passed = scene.save(captures.filePath(name + QStringLiteral("-scene.png")))
                         && desktop.save(captures.filePath(name + QStringLiteral("-desktop.png"))) && passed;
                qInfo() << "Title surface at zero opacity:" << name << inkBackground << surfaceValid;
            }
    root->setProperty("appearancePreviewActive", false);
    QMetaObject::invokeMethod(root, "applyWindowAppearance");
    // Width policy is measured on the real instantiated delegates.
    for (const int width : {1120, 600})
    {
        window.resize(width, 740);
        processWindowEventsFor(650ms);
        qreal tabWidth = 0;
        for (const auto &id : ids)
        {
            auto *tab = quickItem(root, qPrintable(QStringLiteral("workspaceTitle-") + id));
            if (!tab)
                return false;
            if (tabWidth > 0)
                passed = qAbs(tabWidth - tab->width()) < 0.5 && passed;
            tabWidth = tab->width();
            auto *indicator = quickItem(tab, "tabActiveIndicator");
            passed = indicator && indicator->width() >= tabWidth * 0.5 && indicator->width() < tabWidth && passed;
        }
        passed = tabWidth >= 38 && tabWidth <= 184 && passed;
        qInfo() << "Equal-width title tabs: window=" << width << "Tab=" << tabWidth << "passed=" << passed;
    }
    if (args.contains(QStringLiteral("--title-bar-desktop-review")))
    {
        window.resize(1120, 740);
        controller.saveTerminalTheme(QStringLiteral("ztermy-dark"));
        controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), true}});
        QCursor::setPos(window.mapToGlobal(QPoint{500, 250}));
        qInfo() << "Desktop review: owned window for Windows MCP interaction";
        processWindowEventsFor(120s);
    }
    return passed;
}
} // namespace ztermy::ui
