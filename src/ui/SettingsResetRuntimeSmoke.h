#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

namespace ztermy::ui
{
inline bool verifySettingsReset(NativeWindow &window, AppController &controller, const QString &directory)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    window.setGeometry(80, 60, 1100, 860);
    showForRuntimeSmoke(window);
    window.requestActivate();
    if (!controller.saveTerminalTheme(QStringLiteral("nord"))
        || !controller.saveLocalShellPreference(QStringLiteral("commandPrompt"))
        || !controller.saveWindowInteractionSettings(
            {{QStringLiteral("autoHideTitleBar"), true}, {QStringLiteral("tabWidthMode"), QStringLiteral("active")}})
        || !controller.saveWindowsIntegrationSettings(
            {{QStringLiteral("menuMode"), QStringLiteral("submenu")},
             {QStringLiteral("singleShell"), QStringLiteral("commandPrompt")},
             {QStringLiteral("submenuShells"), QStringList{QStringLiteral("commandPrompt")}}})
        || !QMetaObject::invokeMethod(root, "openSettingsTab"))
        return false;
    processWindowEventsFor(250ms);
    auto *pane = quickItem(root, "settingsPane");
    auto *behavior = quickItem(root, "settingsWindowBehaviorCard");
    auto *theme = quickItem(root, "settingsTerminalTheme");
    auto *windows = quickItem(root, "settingsWindowsIntegrationCard");
    auto *size = quickItem(root, "settingsFontSize");
    auto *opacity = quickItem(root, "settingsOpacity");
    auto *hidden = quickItem(root, "settingsSftpShowHidden");
    auto *confirm = quickItem(root, "settingsSftpConfirmDelete");
    if (!pane || !behavior || !theme || !windows || !size || !opacity || !hidden || !confirm)
        return false;
    const auto readDocument = [&] {
        QFile file(QDir(directory).filePath(QStringLiteral("settings.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto activate = [&](const char *name) {
        if (!focusItem(window, quickItem(root, name), QString::fromLatin1(name)))
            return false;
        sendKey(window, Qt::Key_Space);
        processWindowEventsFor(200ms);
        return true;
    };
    pane->setProperty("currentCategory", QStringLiteral("terminal"));
    size->setProperty("value", 1650);
    opacity->setProperty("value", 0.75);
    hidden->setProperty("checked", true);
    confirm->setProperty("checked", false);
    behavior->setProperty("closeToTray", true);
    behavior->setProperty("retainHistoryOnReconnect", true);
    if (!activate("settingsApply"))
        return false;
    const auto defaults = controller.applicationSettingsDefaults();
    const QMap<QString, QSet<QString>> categoryKeys{
        {QStringLiteral("appearance"),
         {QStringLiteral("theme"), QStringLiteral("terminalTheme"), QStringLiteral("lightTheme"),
          QStringLiteral("darkTheme"), QStringLiteral("backdrop"), QStringLiteral("backdropOpacity"),
          QStringLiteral("effectsTier"), QStringLiteral("accent"), QStringLiteral("customAccent"),
          QStringLiteral("uiFontFamily"), QStringLiteral("language")}},
        {QStringLiteral("application"),
         {QStringLiteral("closeToTray"), QStringLiteral("performanceMode"), QStringLiteral("closePaneOnSessionEnd"),
          QStringLiteral("retainHistoryOnReconnect"), QStringLiteral("preserveTerminalSessions"),
          QStringLiteral("reopenLocalSessions"), QStringLiteral("reconnectRemoteSessions"),
          QStringLiteral("allowTerminalTitleChanges"), QStringLiteral("restoreDetachedWindows"),
          QStringLiteral("windowInteraction")}},
        {QStringLiteral("terminal"),
         {QStringLiteral("terminalFontFamily"),
          QStringLiteral("terminalFontSize"),
          QStringLiteral("terminalFontWeight"),
          QStringLiteral("terminalLineHeight"),
          QStringLiteral("terminalCellWidth"),
          QStringLiteral("showAllTerminalFonts"),
          QStringLiteral("terminalLigatures"),
          QStringLiteral("localShell"),
          QStringLiteral("cursor"),
          QStringLiteral("cursorBlink"),
          QStringLiteral("copyOnSelect"),
          QStringLiteral("keepSelectionAfterCopy"),
          QStringLiteral("confirmMultilinePaste"),
          QStringLiteral("terminalRightClick"),
          QStringLiteral("terminalMiddleClick"),
          QStringLiteral("terminalWordDelimiters"),
          QStringLiteral("terminalScrollRows"),
          QStringLiteral("terminalHistoryLines"),
          QStringLiteral("terminalSelectionPopupEnabled"),
          QStringLiteral("terminalSelectionActionOrder"),
          QStringLiteral("terminalSelectionPrimaryActions"),
          QStringLiteral("terminalSelectionRetainActions")}},
        {QStringLiteral("sftp"), {QStringLiteral("sftpShowHiddenFiles"), QStringLiteral("sftpConfirmDelete")}},
        {QStringLiteral("windows"), {QStringLiteral("windowsIntegration")}},
    };
    const auto isDefault = [&](const QString &category) {
        if (category == QStringLiteral("terminal"))
            return size->property("value").toInt()
                   == qRound(defaults.value(QStringLiteral("terminalFontSize")).toDouble() * 100);
        if (category == QStringLiteral("appearance"))
            return opacity->property("value").toDouble() == defaults.value(QStringLiteral("backdropOpacity")).toDouble()
                   && theme->property("mode").toString() == QStringLiteral("system");
        if (category == QStringLiteral("application"))
            return !behavior->property("closeToTray").toBool() && !behavior->property("autoHideTitleBar").toBool()
                   && !behavior->property("retainHistoryOnReconnect").toBool();
        if (category == QStringLiteral("sftp"))
            return !hidden->property("checked").toBool() && confirm->property("checked").toBool();
        return windows->property("menuMode").toString() == QStringLiteral("single")
               && windows->property("singleShell").toString() == QStringLiteral("automatic");
    };
    for (const auto *categoryName : {"terminal", "appearance", "application", "sftp", "windows"})
    {
        const auto category = QString::fromLatin1(categoryName);
        pane->setProperty("currentCategory", category);
        QMetaObject::invokeMethod(pane, "loadDraft");
        processWindowEventsFor(200ms);
        const auto before = readDocument();
        const auto savedSize = size->property("value");
        const auto savedOpacity = opacity->property("value");
        const auto savedTray = behavior->property("closeToTray");
        const auto savedHidden = hidden->property("checked");
        const auto savedMenu = windows->property("menuMode");
        const auto savedChrome = root->property("chromeColor");
        const auto savedAutoHide = root->property("autoHideTitleBar");
        // Preserve unrelated, unsaved drafts too, not only their durable values.
        if (category == QStringLiteral("terminal"))
            opacity->setProperty("value", 0.65);
        if (!activate("settingsReset") || !isDefault(category) || readDocument() != before
            || (category != QStringLiteral("terminal") && size->property("value") != savedSize)
            || (category != QStringLiteral("appearance") && category != QStringLiteral("terminal")
                && opacity->property("value") != savedOpacity)
            || (category == QStringLiteral("terminal") && opacity->property("value").toDouble() != 0.65)
            || (category != QStringLiteral("application") && behavior->property("closeToTray") != savedTray)
            || (category != QStringLiteral("sftp") && hidden->property("checked") != savedHidden)
            || (category != QStringLiteral("windows") && windows->property("menuMode") != savedMenu))
        {
            qWarning() << "Category reset changed durable settings or unrelated drafts:" << category;
            return false;
        }
        if (!activate("settingsDiscard") || readDocument() != before || size->property("value") != savedSize
            || opacity->property("value") != savedOpacity || behavior->property("closeToTray") != savedTray
            || hidden->property("checked") != savedHidden || windows->property("menuMode") != savedMenu
            || root->property("chromeColor") != savedChrome || root->property("autoHideTitleBar") != savedAutoHide)
        {
            qWarning() << "Category reset could not be discarded, including its preview:" << category;
            return false;
        }
        if (!activate("settingsReset") || !activate("settingsApply") || !isDefault(category))
            return false;
        const auto oldSettings = QJsonDocument::fromJson(before).object();
        const auto applied = QJsonDocument::fromJson(readDocument()).object();
        if (applied.isEmpty() || applied == oldSettings)
            return false;
        const auto allowed = categoryKeys.value(category);
        for (auto it = oldSettings.begin(); it != oldSettings.end(); ++it)
            if (applied.value(it.key()) != it.value() && !allowed.contains(it.key()))
            {
                qWarning() << "Category reset applied an unrelated setting:" << category << it.key();
                return false;
            }
        qInfo() << "Category reset draft/discard/apply passed:" << category;
    }
    return true;
}
} // namespace ztermy::ui
