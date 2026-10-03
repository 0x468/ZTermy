#pragma once

#include <QQmlComponent>
#include <QScopeGuard>
#include <QWheelEvent>
#include "ui/RuntimeSmokeItems.h"
#include "ui/TitleBarInputRuntimeSmoke.h"

namespace ztermy::ui
{

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
            passed = processWindowEventsUntil(
                         [&] {
                             auto *bar = quickItem(root, "mainTitleBar");
                             return root->property("titleBarShown").toBool() && bar
                                    && bar->property("revealProgress").toReal() == 1;
                         },
                         2s)
                     && passed;
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
                passed = processWindowEventsUntil(
                             [&] {
                                 auto *bar = quickItem(root, "mainTitleBar");
                                 return root->property("titleBarShown").toBool() && bar
                                        && bar->property("revealProgress").toReal() == 1;
                             },
                             2s)
                         && passed;
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
                if (!surfaceValid)
                    qInfo() << "Missing overlay state:" << root->property("titleBarShown")
                            << quickItem(root, "mainTitleBar")->property("revealProgress") << QCursor::pos()
                            << window.titleBarPointerInside(trigger) << window.geometry();
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
