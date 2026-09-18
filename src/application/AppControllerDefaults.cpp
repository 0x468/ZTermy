#include "application/AppController.h"

#include "core/config/ApplicationSettings.h"

#include <QVariantMap>

namespace ztermy
{

// Draft defaults for the settings page (UI V2 chapter 5). Rows compare their
// draft against these tokens to offer a per-row reset.
QVariantMap AppController::applicationSettingsDefaults() const
{
    const config::ApplicationSettings defaults;
    return {
        {QStringLiteral("theme"), config::themePreferenceToken(defaults.theme)},
        {QStringLiteral("backdrop"), config::backdropPreferenceToken(defaults.backdrop)},
        {QStringLiteral("backdropOpacity"), defaults.backdropOpacity},
        {QStringLiteral("effectsTier"), config::effectsTierToken(defaults.effectsTier)},
        {QStringLiteral("accent"), config::accentPreferenceToken(defaults.accent)},
        {QStringLiteral("customAccent"), defaults.customAccent},
        {QStringLiteral("uiFontFamily"), defaults.uiFontFamily},
        {QStringLiteral("language"), config::languagePreferenceToken(defaults.language)},
        {QStringLiteral("terminalTheme"), defaults.terminalTheme},
        {QStringLiteral("terminalFontFamily"), defaults.terminalFontFamily},
        {QStringLiteral("terminalFontSize"), defaults.terminalFontSize},
        {QStringLiteral("showAllTerminalFonts"), defaults.showAllTerminalFonts},
        {QStringLiteral("terminalLigatures"), defaults.terminalLigatures},
        {QStringLiteral("terminalBackgroundOpacity"), defaults.terminalBackgroundOpacity},
        {QStringLiteral("localShell"), config::localShellPreferenceToken(defaults.localShell)},
        {QStringLiteral("cursor"), config::cursorPreferenceToken(defaults.cursor)},
        {QStringLiteral("cursorBlink"), defaults.cursorBlink},
        {QStringLiteral("copyOnSelect"), defaults.copyOnSelect},
        {QStringLiteral("keepSelectionAfterCopy"), defaults.keepSelectionAfterCopy},
        {QStringLiteral("confirmMultilinePaste"), defaults.confirmMultilinePaste},
        {QStringLiteral("terminalRightClick"), config::terminalRightClickPreferenceToken(defaults.terminalRightClick)},
        {QStringLiteral("terminalMiddleClick"),
         config::terminalMiddleClickPreferenceToken(defaults.terminalMiddleClick)},
        {QStringLiteral("terminalWordDelimiters"), defaults.terminalWordDelimiters},
        {QStringLiteral("terminalScrollRows"), defaults.terminalScrollRows},
        {QStringLiteral("sftpShowHiddenFiles"), defaults.sftpShowHiddenFiles},
        {QStringLiteral("sftpConfirmDelete"), defaults.sftpConfirmDelete},
    };
}

} // namespace ztermy
