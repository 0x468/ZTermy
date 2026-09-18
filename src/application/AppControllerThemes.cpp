#include "application/AppController.h"

#include "core/config/TerminalThemeCatalog.h"

#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace ztermy
{

namespace
{

[[nodiscard]] QVariantMap themeColors(const config::TerminalTheme &theme)
{
    QVariantList ansi;
    ansi.reserve(static_cast<qsizetype>(theme.scheme.ansi.size()));
    for (const auto &color : theme.scheme.ansi)
    {
        ansi.push_back(config::hexColor(color));
    }
    return {
        {QStringLiteral("id"), theme.id},
        {QStringLiteral("name"), theme.name},
        {QStringLiteral("dark"), theme.dark},
        {QStringLiteral("builtIn"), theme.builtIn},
        {QStringLiteral("background"), config::hexColor(theme.scheme.background)},
        {QStringLiteral("foreground"), config::hexColor(theme.scheme.foreground)},
        {QStringLiteral("cursor"), config::hexColor(theme.scheme.cursor)},
        {QStringLiteral("selectionBackground"), config::hexColor(theme.selectionBackground)},
        {QStringLiteral("selectionForeground"), config::hexColor(theme.selectionForeground)},
        {QStringLiteral("accent"), theme.accent},
        {QStringLiteral("ansi"), ansi},
    };
}

[[nodiscard]] QString importErrorToken(const config::TerminalThemeImportError error)
{
    switch (error)
    {
        case config::TerminalThemeImportError::unreadable:
            return QStringLiteral("unreadable");
        case config::TerminalThemeImportError::unsupportedFormat:
            return QStringLiteral("unsupportedFormat");
        case config::TerminalThemeImportError::invalidColor:
            return QStringLiteral("invalidColor");
        case config::TerminalThemeImportError::writeFailed:
            return QStringLiteral("writeFailed");
    }
    return QStringLiteral("unknown");
}

} // namespace

config::TerminalTheme AppController::activeTerminalTheme() const
{
    const bool preferDark = m_settings.theme != config::ThemePreference::light;
    if (!m_previewTerminalThemeId.isEmpty())
    {
        if (auto preview = m_terminalThemes.find(m_previewTerminalThemeId))
        {
            return *preview;
        }
    }
    return m_terminalThemes.resolve(m_settings.terminalTheme, preferDark);
}

void AppController::applyTerminalTheme(TerminalTab &tab) const
{
    const config::TerminalTheme theme = activeTerminalTheme();
    if (tab.local)
    {
        tab.local->setColorScheme(theme.scheme);
    }
    if (tab.ssh)
    {
        tab.ssh->setColorScheme(theme.scheme);
    }
}

void AppController::applyTerminalThemeToSessions()
{
    for (const auto &tab : m_tabs)
    {
        applyTerminalTheme(*tab);
    }
    emit terminalThemeChanged();
}

QString AppController::terminalThemeId() const
{
    return activeTerminalTheme().id;
}

QVariantMap AppController::terminalThemeColors() const
{
    return themeColors(activeTerminalTheme());
}

QVariantList AppController::terminalThemes() const
{
    QVariantList themes;
    const auto &entries = m_terminalThemes.themes();
    themes.reserve(static_cast<qsizetype>(entries.size()));
    for (const auto &theme : entries)
    {
        themes.push_back(themeColors(theme));
    }
    return themes;
}

QVariantMap AppController::terminalThemeColorsFor(const QString &id) const
{
    const auto theme = m_terminalThemes.find(id.trimmed());
    return theme ? themeColors(*theme) : QVariantMap{};
}

bool AppController::saveTerminalTheme(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (!m_terminalThemes.find(trimmed))
    {
        return false;
    }
    const bool previewing = !m_previewTerminalThemeId.isEmpty();
    m_previewTerminalThemeId.clear();
    auto updated = m_settings;
    updated.terminalTheme = trimmed;
    const bool changed = updated.terminalTheme != m_settings.terminalTheme;
    if (!persistApplicationSettings(updated))
    {
        return false;
    }
    // persistApplicationSettings only re-applies on a change; a preview that
    // ended on the persisted theme still needs the sessions refreshed.
    if (!changed && previewing)
    {
        applyTerminalThemeToSessions();
    }
    return true;
}

void AppController::previewTerminalTheme(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed == m_previewTerminalThemeId || !m_terminalThemes.find(trimmed))
    {
        return;
    }
    m_previewTerminalThemeId = trimmed;
    applyTerminalThemeToSessions();
}

void AppController::endTerminalThemePreview()
{
    if (m_previewTerminalThemeId.isEmpty())
    {
        return;
    }
    m_previewTerminalThemeId.clear();
    applyTerminalThemeToSessions();
}

QVariantMap AppController::importTerminalThemeFile(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    auto imported = m_terminalThemes.importFile(path);
    if (!imported)
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), importErrorToken(imported.error())}};
    }
    QVariantList ids;
    ids.reserve(static_cast<qsizetype>(imported->size()));
    for (const auto &id : *imported)
    {
        ids.push_back(id);
    }
    emit terminalThemesChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("ids"), ids}};
}

bool AppController::removeTerminalTheme(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (!m_terminalThemes.remove(trimmed))
    {
        return false;
    }
    if (m_previewTerminalThemeId == trimmed)
    {
        m_previewTerminalThemeId.clear();
    }
    emit terminalThemesChanged();
    if (m_settings.terminalTheme == trimmed)
    {
        // The persisted id now resolves to the built-in default.
        applyTerminalThemeToSessions();
    }
    return true;
}

} // namespace ztermy
