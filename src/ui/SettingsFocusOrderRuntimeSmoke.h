#pragma once

#include <QLoggingCategory>
#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"
#include <array>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(applicationLog)

namespace ztermy::ui
{
[[nodiscard]] inline bool verifySettingsTabOrder(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                                 QQuickItem *rootObject)
{
    const auto verifyOrder = [&window, rootObject](const auto &order) {
        return ztermy::ui::verifySettingsFocusOrder(window, rootObject, order);
    };

    QQuickItem *appearanceCategory = quickItem(rootObject, "settingsAppearanceCategory");
    if (!focusItem(window, appearanceCategory, QStringLiteral("settingsAppearanceCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *settingsPane = rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    if (settingsPane == nullptr || settingsPane->property("currentCategory").toString() != QStringLiteral("appearance"))
    {
        qCWarning(applicationLog) << "Appearance settings category did not activate from the keyboard";
        return false;
    }
    if (!verifyOrder(std::array{"settingsLanguage", "settingsUiFont", "settingsThemeSystem", "settingsThemeFixed"})
        || !verifyOrder(std::array{"settingsAccent", "settingsEffectsTier", "settingsBackdrop", "settingsOpacity",
                                   "settingsReset", "settingsDiscard", "settingsApply"}))
    {
        return false;
    }

    QQuickItem *sftpCategory = quickItem(rootObject, "settingsSftpCategory");
    if (!focusItem(window, sftpCategory, QStringLiteral("settingsSftpCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("sftp"))
    {
        qCWarning(applicationLog) << "SFTP settings category did not activate from the keyboard";
        return false;
    }
    constexpr std::array sftpOrder{
        "settingsSftpShowHidden", "settingsSftpConfirmDelete", "settingsReset", "settingsDiscard", "settingsApply",
    };
    if (!verifyOrder(sftpOrder))
    {
        return false;
    }

    QQuickItem *securityCategory = quickItem(rootObject, "settingsSecurityCategory");
    if (!focusItem(window, securityCategory, QStringLiteral("settingsSecurityCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("security"))
    {
        qCWarning(applicationLog) << "Security settings category did not activate from the keyboard";
        return false;
    }
    QQuickItem *portablePassword = quickItem(rootObject, "settingsPortableVaultPassword");
    QQuickItem *portablePasswordConfirm = quickItem(rootObject, "settingsPortableVaultPasswordConfirm");
    if (portablePassword == nullptr || portablePasswordConfirm == nullptr
        || !portablePassword->setProperty("text", QStringLiteral("keyboard-vault-password"))
        || !portablePasswordConfirm->setProperty("text", QStringLiteral("keyboard-vault-password")))
    {
        return false;
    }
    constexpr std::array securityOrder{
        "settingsCredentialStorage",     "settingsCredentialRemoveSource",
        "settingsPortableVaultPassword", "settingsPortableVaultPasswordConfirm",
        "settingsPortableVaultAction",   "settingsCredentialCleanupStorage",
        "settingsRemoveAllCredentials",
    };
    if (!verifyOrder(securityOrder))
    {
        return false;
    }

    QQuickItem *terminalCategory = quickItem(rootObject, "settingsTerminalCategory");
    if (!focusItem(window, terminalCategory, QStringLiteral("settingsTerminalCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("terminal"))
    {
        qCWarning(applicationLog) << "Terminal settings category did not activate from the keyboard";
        return false;
    }
    std::vector<const char *> terminalOrder{
        "settingsFontFamily",
        "settingsShowAllTerminalFonts",
        "settingsFontSize",
        "settingsFontWeight",
        "settingsLineHeight",
        "settingsCellWidth",
        "settingsLocalShell",
        "settingsLocalShellRefresh",
        "settingsCursor",
        "settingsCursorBlink",
        "settingsCopyOnSelect",
        "settingsKeepSelectionAfterCopy",
        "settingsMultilinePaste",
        "settingsTerminalRightClick",
        "settingsTerminalMiddleClick",
        "settingsTerminalWordDelimiters",
        "settingsTerminalWheelRows",
        "settingsTerminalHistoryLines",
        "settingsSelectionPopupEnabled",
        "settingsSelectionPopupReset",
        "settingsSelectionActionDirect-copy",
        "settingsSelectionActionRetain-copy",
        "settingsSelectionActionDown-copy",
        "settingsSelectionActionDirect-ai",
        "settingsSelectionActionRetain-ai",
        "settingsSelectionActionUp-ai",
        "settingsSelectionActionDown-ai",
        "settingsSelectionActionDirect-search",
        "settingsSelectionActionRetain-search",
        "settingsSelectionActionUp-search",
        "settingsSelectionActionDown-search",
        "settingsSelectionActionDirect-highlight",
        "settingsSelectionActionRetain-highlight",
        "settingsSelectionActionUp-highlight",
        "settingsSelectionActionDown-highlight",
        "settingsSelectionActionDirect-unhighlight",
        "settingsSelectionActionRetain-unhighlight",
        "settingsSelectionActionUp-unhighlight",
        "settingsReset",
        "settingsDiscard",
        "settingsApply",
    };
    QQuickItem *terminalLigatures = quickItem(rootObject, "settingsTerminalLigatures");
    if (terminalLigatures != nullptr && terminalLigatures->isEnabled())
    {
        terminalOrder.insert(terminalOrder.begin() + 6, "settingsTerminalLigatures");
    }
    if (!verifyOrder(terminalOrder))
    {
        return false;
    }

    QQuickItem *shortcutsCategory = quickItem(rootObject, "settingsShortcutsCategory");
    if (!focusItem(window, shortcutsCategory, QStringLiteral("settingsShortcutsCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("shortcuts"))
    {
        qCWarning(applicationLog) << "Shortcuts settings category did not activate from the keyboard";
        return false;
    }

    QQuickItem *shortcutRecorder = visualQuickItem(rootObject, "shortcutRecorder_application.commandPalette");
    if (!focusItem(window, shortcutRecorder, QStringLiteral("shortcutRecorder_application.commandPalette")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool recordingStarted = settingsPane->property("shortcutRecording").toBool();
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool conflictRejected = settingsPane->property("shortcutRecording").toBool();
    sendKey(window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    const auto commandPaletteShortcut = [&controller]() {
        const QVariantList actions = controller.actions();
        for (const QVariant &entry : actions)
        {
            const QVariantMap action = entry.toMap();
            if (action.value(QStringLiteral("id")).toString() == QStringLiteral("application.commandPalette"))
            {
                return action.value(QStringLiteral("shortcut")).toString();
            }
        }
        return QString{};
    };
    const bool shortcutRecorded = !settingsPane->property("shortcutRecording").toBool()
                                  && commandPaletteShortcut() == QStringLiteral("Ctrl+Alt+P");
    const bool shortcutReset = controller.resetActionShortcut(QStringLiteral("application.commandPalette"))
                               && commandPaletteShortcut() == QStringLiteral("Ctrl+Shift+P");
    if (!recordingStarted || !conflictRejected || !shortcutRecorded || !shortcutReset)
    {
        qCWarning(applicationLog) << "Shortcut recorder keyboard route failed"
                                  << "recordingStarted=" << recordingStarted << "conflictRejected=" << conflictRejected
                                  << "shortcutRecorded=" << shortcutRecorded << "shortcutReset=" << shortcutReset
                                  << "effectiveShortcut=" << commandPaletteShortcut();
        return false;
    }
    if (!focusItem(window, terminalCategory, QStringLiteral("settingsTerminalCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("terminal"))
    {
        qCWarning(applicationLog) << "Terminal settings category was not restored after shortcut recording";
        return false;
    }
    return true;
}

} // namespace ztermy::ui
