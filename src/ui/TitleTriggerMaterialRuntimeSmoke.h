#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QPointer>
#include <QScopeGuard>

namespace ztermy::ui
{
// Real QML/DWM captures. Refuse ordinary launches, data and pre-existing sessions.
inline bool verifyTitleTriggerMaterial(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    const auto arguments = QCoreApplication::arguments();
    const auto data = arguments.indexOf(QStringLiteral("--data-dir"));
    if (qEnvironmentVariable("ZTERMY_TEST_ISOLATED_SHELLS") != QStringLiteral("1") || data < 0
        || data + 1 >= arguments.size() || !controller.terminalTabs().isEmpty())
        return false;
    const QDir expectedData(QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("test-data/title-trigger-material-runtime")));
    if (QDir(arguments[data + 1]).canonicalPath().isEmpty()
        || QDir(arguments[data + 1]).canonicalPath() != expectedData.canonicalPath())
        return false;
    QDir captures(expectedData.filePath(QStringLiteral("captures")));
    if (!captures.mkpath(QStringLiteral(".")))
        return false;
    auto *root = window.rootObject();
    const auto pointer = QCursor::pos();
    const bool topmost = window.alwaysOnTop();
    const bool requestedTopmost = root->property("windowAlwaysOnTopRequested").toBool();
    const auto originalTheme = controller.terminalThemeId();
    const auto originalInteraction = controller.windowInteractionSettings();
    const auto cleanup = qScopeGuard([&] {
        QCursor::setPos(pointer);
        root->setProperty("windowAlwaysOnTopRequested", requestedTopmost);
        window.setAlwaysOnTop(topmost);
        controller.saveTerminalTheme(originalTheme);
        controller.saveWindowInteractionSettings(originalInteraction);
        root->setProperty("appearancePreviewActive", false);
        QMetaObject::invokeMethod(root, "applyWindowAppearance");
    });
    window.setGeometry(80, 100, 660, 480);
    window.hide();
    window.show();
    root->setProperty("windowAlwaysOnTopRequested", true);
    window.setAlwaysOnTop(true);
    QCursor::setPos(window.mapToGlobal(QPoint{400, 300}));
    if (!controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), true}}))
        return false;
    const auto mainId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto detachedId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto closeFixtures = qScopeGuard([&] {
        if (!detachedId.isEmpty())
            controller.closeTerminalTab(detachedId);
        if (!mainId.isEmpty())
            controller.closeTerminalTab(mainId);
    });
    if (mainId.isEmpty() || detachedId.isEmpty() || !controller.detachTerminalWorkspace(detachedId))
        return false;
    processWindowEventsFor(500ms);
    const auto owner = controller.terminalWorkspace(detachedId).value(QStringLiteral("windowId")).toString();
    QPointer<QQuickWindow> detached;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == owner)
            detached = qobject_cast<QQuickWindow *>(candidate);
    if (!detached)
        return false;
    detached->setGeometry(780, 100, 660, 480);
    detached->hide();
    detached->show();
    const auto handle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
    SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    QMetaObject::invokeMethod(root, "activateMainTerminal", Q_ARG(QVariant, mainId));
    auto *strip = quickItem(root, "mainTitleTriggerStrip");
    auto *navigation = quickItem(root, "workspaceNavigation");
    auto *navigationStrip = quickItem(root, "mainTitleTriggerNavigation");
    auto *detachedStrip = quickItem(detached->contentItem(), "detachedTitleTriggerStrip");
    auto *detachedTint = quickItem(detached->contentItem(), "detachedWorkspaceBackground");
    if (!strip || !navigation || !navigationStrip || !detachedStrip || !detachedTint)
        return false;
    const auto matches = [](const QColor left, const QColor right) {
        return qAbs(left.alpha() - right.alpha()) <= 1
               && (left.alpha() < 5
                   || (qAbs(left.red() - right.red()) <= 2 && qAbs(left.green() - right.green()) <= 2
                       && qAbs(left.blue() - right.blue()) <= 2));
    };
    const auto capture = [&](QQuickWindow &target, const QString &name, const QColor expected) {
        const QImage scene = target.grabWindow();
        if (scene.isNull())
            return false;
        const QPoint top = target.mapToGlobal(QPoint{}) - target.screen()->geometry().topLeft();
        const auto desktop = target.screen()->grabWindow(0, top.x(), top.y(), target.width(), target.height());
        const auto pixel =
            scene.pixelColor(qRound(500 * scene.devicePixelRatio()), qRound(6 * scene.devicePixelRatio()));
        const bool coherent = matches(pixel, expected);
        qInfo() << "Hidden strip surface:" << name << "pixel=" << pixel << "expected=" << expected << coherent;
        return coherent && scene.save(captures.filePath(name + QStringLiteral("-scene.png")))
               && desktop.save(captures.filePath(name + QStringLiteral("-desktop.png")));
    };
    bool passed = true;
    for (const auto *theme : {"ztermy-light", "ztermy-dark"})
        for (const auto *material : {"solid", "transparent", "aero", "acrylic", "mica", "micaAlt"})
            for (const double opacity : {0.0, 0.45, 1.0})
            {
                passed = controller.saveTerminalTheme(QString::fromLatin1(theme)) && passed;
                const auto name = QStringLiteral("%1-%2-%3")
                                      .arg(QLatin1StringView{theme}, QLatin1StringView{material})
                                      .arg(qRound(opacity * 100));
                for (const bool mainActive : {true, false})
                {
                    auto *activeWindow = mainActive ? static_cast<QQuickWindow *>(&window) : detached.data();
                    activeWindow->raise();
                    activeWindow->requestActivate();
                    const auto activeHandle =
                        reinterpret_cast<HWND>(activeWindow->winId()); // NOLINT(performance-no-int-to-ptr)
                    SetForegroundWindow(activeHandle);
                    if (!processWindowEventsUntil(
                            [&] {
                                return activeWindow->isActive();
                            },
                            2s))
                    {
                        qWarning() << "Cannot establish owned fixture focus:" << mainActive << window.isActive()
                                   << detached->isActive() << (GetForegroundWindow() == activeHandle);
                        return false;
                    }
                    const auto phase = mainActive ? QStringLiteral("main-active") : QStringLiteral("detached-active");
                    for (const auto *page : {"settings", "hosts", "terminal"})
                    {
                        root->setProperty("currentPage", QString::fromLatin1(page));
                        // Navigation dismisses Settings preview by design. Apply the
                        // requested fixture after that lifecycle, never test defaults
                        // under a filename claiming another material/opacity.
                        processWindowEventsFor(20ms);
                        root->setProperty("previewBackdropPreference", QString::fromLatin1(material));
                        root->setProperty("previewBackdropOpacity", opacity);
                        root->setProperty("appearancePreviewActive", true);
                        QMetaObject::invokeMethod(root, "applyWindowAppearance");
                        processWindowEventsFor(80ms);
                        passed = !root->property("titleBarInteractive").toBool()
                                 && !detached->property("windowChromeInteractive").toBool() && passed;
                        auto expected =
                            root->property(QLatin1StringView{page} == "terminal" ? "workspaceColor" : "contentColor")
                                .value<QColor>();
                        if (expected.alpha() == 0)
                            expected.setAlpha(1);
                        if (QLatin1StringView{page} == "terminal")
                        {
                            const qreal alpha = QLatin1StringView{material} == "solid"     ? 1.0
                                                : QLatin1StringView{material} == "mica"    ? 0.88
                                                : QLatin1StringView{material} == "micaAlt" ? 0.92
                                                                                           : opacity;
                            passed = qAbs(expected.alpha() - std::max(1, qRound(alpha * 255))) <= 1 && passed;
                        }
                        passed = capture(window,
                                         name + QLatin1Char{'-'} + QLatin1StringView{page} + QLatin1Char{'-'} + phase,
                                         expected)
                                 && passed;
                        if (QLatin1StringView{page} == "hosts")
                            passed = navigationStrip->isVisible()
                                     && qAbs(navigationStrip->width() - navigation->width()) < 0.01
                                     && navigationStrip->property("color") == navigation->property("color") && passed;
                    }
                    auto expected = detachedTint->property("color").value<QColor>();
                    if (expected.alpha() == 0)
                        expected.setAlpha(1);
                    passed = capture(*detached, name + QStringLiteral("-detached-") + phase, expected) && passed;
                    const auto floor = detachedStrip->property("color").value<QColor>();
                    passed = floor.alpha() == (expected.alpha() == 1 ? 1 : 0) && passed;
                }
            }
    qInfo() << "Title trigger material matrix:" << passed;
    return passed;
}
} // namespace ztermy::ui
