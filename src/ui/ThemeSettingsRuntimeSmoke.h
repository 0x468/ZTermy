#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

namespace ztermy::ui
{
[[nodiscard]] inline bool verifyBackdropOpacity(QQuickItem *root)
{
    auto *backdrop = quickItem(root, "settingsBackdrop");
    auto *opacity = quickItem(root, "settingsOpacity");
    if (!backdrop || !opacity)
        return false;
    const auto previous = backdrop->property("currentIndex");
    bool passed = true;
    for (const int index : {0, 1, 2, 5})
    {
        backdrop->setProperty("currentIndex", index);
        processWindowEventsFor(std::chrono::milliseconds{50});
        const bool adjustable = opacity->isVisible() && opacity->isEnabled();
        passed = passed && adjustable == (index != 5);
    }
    backdrop->setProperty("currentIndex", previous);
    processWindowEventsFor(std::chrono::milliseconds{50});
    qInfo() << "Acrylic, glass and transparent opacity controls:" << passed;
    return passed;
}

// Exercise the real draft control and Apply/Discard buttons. In particular,
// compare the durable document during preview, not only the rendered value.
[[nodiscard]] inline bool verifyTitleBarSettingsPreview(NativeWindow &window, AppController &controller,
                                                        const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    const auto previousPointer = QCursor::pos();
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(previousPointer);
    });
    const auto arguments = QCoreApplication::arguments();
    const auto data = arguments.indexOf(QStringLiteral("--data-dir"));
    if (data < 0 || data + 1 >= arguments.size())
        return false;
    const auto readSettings = [&] {
        QFile file(QDir(arguments[data + 1]).filePath(QStringLiteral("settings.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto activate = [&](const char *name) {
        if (!focusItem(window, quickItem(root, name), QString::fromLatin1(name)))
            return false;
        sendKey(window, Qt::Key_Space);
        QCursor::setPos(window.mapToGlobal(QPoint{500, 300}));
        processWindowEventsFor(150ms);
        return true;
    };
    const auto savedAutomatic = [&] {
        return controller.windowInteractionSettings().value(QStringLiteral("autoHideTitleBar")).toBool();
    };
    const auto leave = [&] {
        root->setProperty("currentPage", QStringLiteral("hosts"));
        processWindowEventsFor(250ms);
    };
    // Reopen through the same command used by Settings shortcuts: a hidden
    // caption button cannot be focused. Hover itself has a separate smoke.
    const auto reopen = [&] {
        const bool invoked = QMetaObject::invokeMethod(root, "openSettingsTab");
        processWindowEventsFor(250ms);
        return invoked && root->property("currentPage").toString() == QStringLiteral("settings");
    };
    QCursor::setPos(window.mapToGlobal(QPoint{500, 300}));
    leave();
    if (!controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), false}})
        || !activate("settingsShortcutAction") || !activate("settingsApplicationCategory"))
        return false;
    const auto originalDocument = readSettings();
    if (originalDocument.isEmpty() || !activate("settingsAutoHideTitleBarSwitch"))
        return false;
    const bool hidden = processWindowEventsUntil(
        [&] {
            return !root->property("titleBarInteractive").toBool();
        },
        2s);
    const bool preview = root->property("autoHideTitleBar").toBool()
                         && root->property("reservedTitleHeight").toInt() < root->property("titleBarHeight").toInt()
                         && hidden && !savedAutomatic() && readSettings() == originalDocument;
    const bool captured =
        window.grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("title-preview-auto.png")));
    if (!activate("settingsDiscard"))
        return false;
    const bool discarded = !root->property("autoHideTitleBar").toBool()
                           && root->property("reservedTitleHeight") == root->property("titleBarHeight")
                           && !savedAutomatic() && readSettings() == originalDocument;
    if (!activate("settingsAutoHideTitleBarSwitch"))
        return false;
    leave();
    const bool leftUnsaved = !root->property("autoHideTitleBar").toBool()
                             && !root->property("windowInteractionPreviewActive").toBool() && !savedAutomatic()
                             && readSettings() == originalDocument;
    if (!reopen())
        return false;
    auto *toggle = quickItem(root, "settingsAutoHideTitleBarSwitch");
    const bool reloaded = toggle && !toggle->property("checked").toBool();
    if (!activate("settingsAutoHideTitleBarSwitch") || !activate("settingsApply"))
        return false;
    leave();
    const auto persisted = QJsonDocument::fromJson(readSettings())
                               .object()
                               .value(QStringLiteral("windowInteraction"))
                               .toObject()
                               .value(QStringLiteral("autoHideTitleBar"));
    const bool applied =
        savedAutomatic() && persisted.isBool() && persisted.toBool() && root->property("autoHideTitleBar").toBool();
    if (!reopen() || !activate("settingsAutoHideTitleBarSwitch"))
        return false;
    const bool disablePreview = !root->property("autoHideTitleBar").toBool()
                                && root->property("reservedTitleHeight") == root->property("titleBarHeight")
                                && savedAutomatic();
    const bool persistentCaptured =
        window.grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("title-preview-persistent.png")));
    if (!activate("settingsDiscard"))
        return false;
    const bool restoredSaved = root->property("autoHideTitleBar").toBool() && savedAutomatic();
    leave();
    // Keep the existing appearance/focus checks in their persistent chrome mode.
    const bool reset = controller.saveWindowInteractionSettings({{QStringLiteral("autoHideTitleBar"), false}})
                       && reopen() && activate("settingsAppearanceCategory");
    qInfo() << "Title bar draft preview: preview, discard, leave, reload, apply, disable, restore:" << preview
            << discarded << leftUnsaved << reloaded << applied << disablePreview << restoredSaved;
    return preview && discarded && leftUnsaved && reloaded && applied && disablePreview && restoredSaved && captured
           && persistentCaptured && reset;
}

[[nodiscard]] inline bool verifyThemeSettings(NativeWindow &window, AppController &controller,
                                              const QString &outputDirectory)
{
    auto *rootObject = window.rootObject();
    const auto capture = [&](const QString &name) {
        return window.grabWindow().save(QDir(outputDirectory).filePath(name + QStringLiteral(".png")));
    };
    const auto activate = [&](const char *name) {
        if (!focusItem(window, quickItem(rootObject, name), QString::fromLatin1(name)))
            return false;
        sendKey(window, Qt::Key_Space);
        processWindowEventsFor(std::chrono::milliseconds{120});
        return true;
    };
    if (!controller.saveTerminalTheme(QStringLiteral("nord")) || !activate("settingsShortcutAction")
        || !activate("settingsAppearanceCategory"))
        return false;
    if (!verifyBackdropOpacity(rootObject))
        return false;
    const auto savedChrome = rootObject->property("chromeColor").value<QColor>();
    if (!focusItem(window, quickItem(rootObject, "themeCard-ztermy-light"), QStringLiteral("themeCard-ztermy-light")))
        return false;
    processWindowEventsFor(std::chrono::milliseconds{120});
    const bool localPreviewOnly = controller.terminalThemeId() == QStringLiteral("nord")
                                  && rootObject->property("chromeColor").value<QColor>() == savedChrome;
    if (!activate("themeCard-ztermy-light"))
        return false;
    const bool wholePreview = controller.terminalThemeId() == QStringLiteral("ztermy-light")
                              && rootObject->property("chromeColor").value<QColor>() != savedChrome;
    const bool captured = capture(QStringLiteral("unified-theme-light"));
    if (!activate("settingsDiscard"))
        return false;
    const bool discarded = controller.terminalThemeId() == QStringLiteral("nord");
    if (!activate("themeCard-ztermy-light") || !activate("settingsApply"))
        return false;
    const bool applied =
        controller.themePolicy().value(QStringLiteral("fixed")).toString() == QStringLiteral("ztermy-light");
    if (!activate("settingsThemeSystem") || !activate("settingsThemeLightSlot")
        || !activate("themeCard-solarized-light") || !activate("settingsThemeDarkSlot")
        || !activate("themeCard-dracula") || !activate("settingsApply"))
        return false;
    controller.setSystemDarkMode(false);
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool light = controller.terminalThemeId() == QStringLiteral("solarized-light");
    controller.setSystemDarkMode(true);
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool dark = controller.terminalThemeId() == QStringLiteral("dracula");
    controller.setSystemDarkMode(window.systemDarkMode());
    if (!activate("settingsThemeDarkSlot"))
        return false;
    if (!verifyTitleBarSettingsPreview(window, controller, outputDirectory))
        return false;
    const bool darkCaptured = capture(QStringLiteral("unified-theme-dark"));
    window.resize(QSize{600, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool compactCaptured = capture(QStringLiteral("unified-theme-compact"));
    qInfo() << "Unified theme runtime check" << "localPreviewOnly=" << localPreviewOnly
            << "wholePreview=" << wholePreview << "discarded=" << discarded << "applied=" << applied
            << "systemLight=" << light << "systemDark=" << dark;
    return localPreviewOnly && wholePreview && discarded && applied && light && dark && captured && darkCaptured
           && compactCaptured;
}
} // namespace ztermy::ui
