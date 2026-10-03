#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/DetachedWindowRuntimeSmoke.h"
#include "ui/PaneLayoutRuntimeSmoke.h"
#include "ui/ResizeRuntimeSmoke.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/SideDrawerRuntimeSmoke.h"
#include "ui/SidePanelLayoutRuntimeSmoke.h"
#include "ui/TitleBarRuntimeSmoke.h"
#include "ui/TitleTriggerMaterialRuntimeSmoke.h"
#include "ui/WorkbenchInteractionRuntimeSmoke.h"
#include "ui/terminal/TerminalPaneRuntimeSmoke.h"

#include <QColor>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <algorithm>
#include <memory>
#include <optional>

namespace ztermy::ui
{
inline std::optional<bool> runWorkbenchRuntimeCheck(NativeWindow &window, AppController &controller,
                                                    const QStringList &arguments)
{
    if (arguments.contains(QStringLiteral("--title-trigger-material-smoke")))
        return verifyTitleTriggerMaterial(window, controller);
    if (arguments.contains(QStringLiteral("--title-bar-immersive-smoke")))
        return verifyImmersiveTitleBar(window, controller);
    if (arguments.contains(QStringLiteral("--material-theme-capture")))
    {
        const auto dataIndex = arguments.indexOf(QStringLiteral("--data-dir"));
        if (dataIndex < 0 || dataIndex + 1 >= arguments.size())
            return false;
        QDir captures(QDir(arguments[dataIndex + 1]).filePath(QStringLiteral("captures")));
        if (!captures.mkpath(QStringLiteral(".")))
            return false;
        QQuickWindow backing;
        backing.setFlags(Qt::Window | Qt::FramelessWindowHint);
        backing.setGeometry(window.screen()->availableGeometry());
        backing.setColor(QColor(QStringLiteral("#238C62")));
        backing.show();
        window.setGeometry(100, 100, 900, 620);
        window.hide();
        window.show();
        auto *root = window.rootObject();
        if (controller.startLocalTerminal().isEmpty())
            return false;
        processWindowEventsFor(std::chrono::milliseconds{800});
        root->setProperty("currentPage", QStringLiteral("terminal"));
        root->setProperty("previewEffectsTier", QStringLiteral("full"));
        root->setProperty("previewBackdropOpacity", 0.0);
        root->setProperty("appearancePreviewActive", true);
        bool passed = true;
        for (const auto *material : {"transparent", "aero", "acrylic", "mica", "micaAlt", "solid"})
            for (const auto *palette : {"ztermy-dark", "ztermy-light"})
            {
                passed = controller.saveTerminalTheme(QString::fromLatin1(palette)) && passed;
                processWindowEventsFor(std::chrono::milliseconds{200});
                root->setProperty("currentPage", QStringLiteral("terminal"));
                root->setProperty("previewEffectsTier", QStringLiteral("full"));
                root->setProperty("previewBackdropOpacity", 0.0);
                root->setProperty("previewBackdropPreference", QString::fromLatin1(material));
                root->setProperty("appearancePreviewActive", true);
                QMetaObject::invokeMethod(root, "applyWindowAppearance");
                window.raise();
                window.requestActivate();
                processWindowEventsFor(std::chrono::milliseconds{600});
                QColor firstDesktop;
                for (const auto *color : {"#238C62", "#BA486D"})
                {
                    backing.setColor(QColor(QString::fromLatin1(color)));
                    backing.lower();
                    window.showNormal();
                    window.setAlwaysOnTop(true);
                    window.raise();
                    processWindowEventsFor(std::chrono::milliseconds{300});
                    const QString name =
                        QStringLiteral("%1-%2-%3")
                            .arg(QLatin1StringView{material}, QLatin1StringView{palette}, QLatin1StringView{color + 1});
                    const auto scene = window.grabWindow();
                    const auto origin = window.mapToGlobal(QPoint{}) - window.screen()->geometry().topLeft();
                    const auto desktop = window.screen()
                                             ->grabWindow(0, origin.x(), origin.y(), window.width(), window.height())
                                             .toImage();
                    if (scene.isNull() || desktop.isNull())
                        return false;
                    const QColor center = desktop.pixelColor(desktop.width() / 2, desktop.height() / 2);
                    // Reject a hidden/occluded test window, even if its backing
                    // colors happen to satisfy the transparency comparison.
                    const QPoint iconPoint(qRound(20 * scene.devicePixelRatio()),
                                           qRound(20 * scene.devicePixelRatio()));
                    const auto icon = scene.pixelColor(iconPoint);
                    const auto screenIcon = desktop.pixelColor(iconPoint);
                    passed = icon.alpha() == 255 && qAbs(icon.red() - screenIcon.red()) < 8
                             && qAbs(icon.green() - screenIcon.green()) < 8 && qAbs(icon.blue() - screenIcon.blue()) < 8
                             && passed;
                    if (!firstDesktop.isValid())
                        firstDesktop = center;
                    else if (QLatin1StringView{material} == "transparent")
                    {
                        const int difference = qAbs(center.red() - firstDesktop.red())
                                               + qAbs(center.green() - firstDesktop.green())
                                               + qAbs(center.blue() - firstDesktop.blue());
                        passed = difference > 40 && passed;
                        qInfo() << "Transparent desktop response" << palette << difference;
                    }
                    else if (QLatin1StringView{material} == "solid")
                        passed = center == firstDesktop && passed;
                    passed = scene.save(captures.filePath(name + QStringLiteral("-scene.png"))) && passed;
                    passed = desktop.save(captures.filePath(name + QStringLiteral("-desktop.png"))) && passed;
                    qInfo() << "Material capture" << name << "rootColor=" << root->property("color")
                            << "clearColor=" << window.color() << "alphaBits=" << window.format().alphaBufferSize()
                            << "sceneCenter=" << scene.pixelColor(scene.width() / 2, scene.height() / 2)
                            << "desktopCenter=" << desktop.pixelColor(desktop.width() / 2, desktop.height() / 2);
                }
                if (QLatin1StringView{material} == "transparent" || QLatin1StringView{material} == "aero"
                    || QLatin1StringView{material} == "acrylic")
                {
                    root->setProperty("previewBackdropOpacity", 1.0);
                    window.requestUpdate();
                    processWindowEventsFor(std::chrono::milliseconds{400});
                    const auto opaqueScene = window.grabWindow();
                    processWindowEventsFor(std::chrono::milliseconds{100});
                    const auto origin = window.mapToGlobal(QPoint{}) - window.screen()->geometry().topLeft();
                    const auto opaque = window.screen()
                                            ->grabWindow(0, origin.x(), origin.y(), window.width(), window.height())
                                            .toImage();
                    if (opaque.isNull())
                        return false;
                    const auto center = opaque.pixelColor(opaque.width() / 2, opaque.height() / 2);
                    const auto expected = root->property("color").value<QColor>();
                    passed = !opaqueScene.isNull()
                             && opaqueScene.pixelColor(opaqueScene.width() / 2, opaqueScene.height() / 2).alpha() == 255
                             && passed;
                    passed = qAbs(center.red() - expected.red()) < 3 && qAbs(center.green() - expected.green()) < 3
                             && qAbs(center.blue() - expected.blue()) < 3 && passed;
                    passed = opaque.save(
                                 captures.filePath(QStringLiteral("%1-%2-opaque.png")
                                                       .arg(QLatin1StringView{material}, QLatin1StringView{palette})))
                             && passed;
                }
            }
        root->setProperty("currentPage", QStringLiteral("hosts"));
        root->setProperty("workspaceNavigationWidth", 220);
        auto *theme = window.engine()->singletonInstance<QObject *>(qmlTypeId("Ztermy", 1, 0, "Theme"));
        if (theme)
            theme->setProperty("animationsEnabled", false);
        root->setProperty("pageReveal", 1.0);
        processWindowEventsFor(std::chrono::milliseconds{1200});
        passed = window.grabWindow().save(captures.filePath(QStringLiteral("host-import-export.png"))) && passed;
        qInfo() << "Material desktop capture checks passed=" << passed;
        return passed;
    }
    if (arguments.contains(QStringLiteral("--resize-interactions-smoke")))
        return verifyResizeInteractions(window, controller);
    if (arguments.contains(QStringLiteral("--detached-material-smoke")))
        return verifyDetachedWindowSurface(window, controller);
    if (arguments.contains(QStringLiteral("--history-scroll-smoke"))
        || arguments.contains(QStringLiteral("--history-autoload-smoke")))
        return verifyHistoryScrollPreservation(window, controller);
    if (arguments.contains(QStringLiteral("--toolbar-hover-smoke")))
        return verifyToolbarHoverAnimation(window, controller);
    if (arguments.contains(QStringLiteral("--pane-scrollbar-smoke")))
        return verifyPaneScrollbarLayout(window, controller);
    if (arguments.contains(QStringLiteral("--script-form-layout-smoke")))
        return verifyScriptFormLayout(window, controller) && verifySidePanelLayout(window, controller);
    if (arguments.contains(QStringLiteral("--profile-card-icons-smoke")))
    {
        window.resize(1000, 760);
        window.show();
        processWindowEventsFor(std::chrono::milliseconds{600});
        bool passed = true;
        for (const auto &[name, expected] :
             {QPair{QStringLiteral("savedHostProfileIcon-target"), QStringLiteral("brand-ubuntu")},
              QPair{QStringLiteral("savedHostProfileIcon-jump"), QStringLiteral("database")},
              QPair{QStringLiteral("recentHostProfileIcon-target"), QStringLiteral("brand-ubuntu")}})
        {
            auto *icon = findWindowSmokeItem(window.contentItem(), name);
            const bool valid = icon && icon->isVisible() && icon->property("name").toString() == expected;
            qInfo() << "Host card profile icon" << name << "passed=" << valid;
            passed = passed && valid;
        }
        return captureWindowSmokeItem(window.contentItem(), QStringLiteral("profile-card-icons.png")) && passed;
    }
    if (arguments.contains(QStringLiteral("--profile-icons-smoke")))
    {
        auto *theme = window.engine()->singletonInstance<QObject *>(qmlTypeId("Ztermy", 1, 0, "Theme"));
        if (!theme)
            return false;
        window.resize(1000, 760);
        window.show();
        bool passed = verifyPaneShellMenu(window);
        int repeats = arguments.contains(QStringLiteral("--repeat-profile-icons")) ? 10 : 1;
        for (const auto &argument : arguments)
            if (argument.startsWith(QStringLiteral("--profile-icon-cycles=")))
                repeats = qBound(1, argument.sliced(22).toInt(), 30);
        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            for (const auto *mode : {"dark", "light"})
            {
                theme->setProperty("terminalPalette", QVariantMap{});
                theme->setProperty("preference", QString::fromLatin1(mode));
                processWindowEventsFor(std::chrono::milliseconds{250});
                passed = passed && theme->property("dark").toBool() == (QLatin1StringView{mode} == "dark");
                passed =
                    verifyHostProfileIconPicker(window, QStringLiteral("profile-icons-%1.png").arg(mode)) && passed;
            }
            qInfo() << "Profile icon lifecycle round=" << repeat + 1
                    << "liveTreeObjects=" << window.contentItem()->findChildren<QObject *>().size();
        }
        return passed;
    }
    return runSideDrawerRuntimeCheck(window, controller, arguments);
}
} // namespace ztermy::ui
