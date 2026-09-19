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
    const bool followSystem = m_settings.theme == config::ThemePreference::system;
    const bool preferDark = followSystem ? m_systemDarkMode : m_settings.theme != config::ThemePreference::light;
    if (!m_previewTerminalThemeId.isEmpty())
    {
        if (auto preview = m_terminalThemes.find(m_previewTerminalThemeId))
        {
            return *preview;
        }
    }
    auto theme = m_terminalThemes.resolve(followSystem ? (preferDark ? m_settings.darkTheme : m_settings.lightTheme)
                                                       : m_settings.terminalTheme,
                                          preferDark);
    if (followSystem && theme.dark != preferDark)
        return m_terminalThemes.resolve({}, preferDark);
    return theme;
}

QVariantMap AppController::themePolicy() const
{
    const auto slot = [this](const QString &id, bool dark) {
        const auto theme = m_terminalThemes.resolve(id, dark);
        return theme.dark == dark ? theme.id : m_terminalThemes.resolve({}, dark).id;
    };
    return {{QStringLiteral("mode"),
             m_settings.theme == config::ThemePreference::system ? QStringLiteral("system") : QStringLiteral("fixed")},
            {QStringLiteral("fixed"),
             m_terminalThemes.resolve(m_settings.terminalTheme, m_settings.theme != config::ThemePreference::light).id},
            {QStringLiteral("light"), slot(m_settings.lightTheme, false)},
            {QStringLiteral("dark"), slot(m_settings.darkTheme, true)}};
}

bool AppController::saveThemePolicy(const QString &mode, const QString &fixed, const QString &light,
                                    const QString &dark)
{
    const auto fixedTheme = m_terminalThemes.find(fixed.trimmed());
    const auto lightTheme = m_terminalThemes.find(light.trimmed());
    const auto darkTheme = m_terminalThemes.find(dark.trimmed());
    if ((mode != QStringLiteral("system") && mode != QStringLiteral("fixed")) || !fixedTheme || !lightTheme
        || !darkTheme || lightTheme->dark || !darkTheme->dark)
        return false;
    auto updated = m_settings;
    updated.theme = mode == QStringLiteral("system") ? config::ThemePreference::system
                    : fixedTheme->dark               ? config::ThemePreference::dark
                                                     : config::ThemePreference::light;
    updated.terminalTheme = fixedTheme->id;
    updated.lightTheme = lightTheme->id;
    updated.darkTheme = darkTheme->id;
    if (!persistApplicationSettings(updated))
        return false;
    endTerminalThemePreview();
    return true;
}

void AppController::setSystemDarkMode(bool dark)
{
    if (m_systemDarkMode == dark)
        return;
    m_systemDarkMode = dark;
    if (m_settings.theme == config::ThemePreference::system && m_previewTerminalThemeId.isEmpty())
        applyTerminalThemeToSessions();
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
    const auto theme = m_terminalThemes.find(trimmed);
    if (!theme)
    {
        return false;
    }
    const bool previewing = !m_previewTerminalThemeId.isEmpty();
    auto updated = m_settings;
    updated.terminalTheme = trimmed;
    updated.theme = theme->dark ? config::ThemePreference::dark : config::ThemePreference::light;
    if (!persistApplicationSettings(updated))
    {
        return false;
    }
    // Keep the preview recoverable until persistence succeeds.
    if (previewing)
    {
        m_previewTerminalThemeId.clear();
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
    const bool previewRemoved = m_previewTerminalThemeId == trimmed;
    if (previewRemoved)
    {
        m_previewTerminalThemeId.clear();
    }
    emit terminalThemesChanged();
    if (previewRemoved || m_settings.terminalTheme == trimmed || m_settings.lightTheme == trimmed
        || m_settings.darkTheme == trimmed)
    {
        // The persisted id now resolves to the built-in default.
        applyTerminalThemeToSessions();
    }
    return true;
}

} // namespace ztermy
