#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QImage>

namespace ztermy::ui
{
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
    const QColor savedChrome = rootObject->property("chromeColor").value<QColor>();
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
