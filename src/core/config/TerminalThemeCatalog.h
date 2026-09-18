#pragma once

#include "core/config/TerminalColorScheme.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace ztermy::config
{

// A terminal theme is the palette layer of ADR 0121: the colors the terminal
// engine renders with, plus the few skin hints (selection, accent) the chrome
// derives its own ladder from. Chrome surfaces never read the ANSI table.
struct TerminalTheme final
{
    QString id;
    QString name;
    bool dark = true;
    bool builtIn = false;
    terminal::TerminalColorScheme scheme;
    terminal::TerminalColor selectionBackground{.red = 42, .green = 91, .blue = 145};
    terminal::TerminalColor selectionForeground{.red = 255, .green = 255, .blue = 255};
    // Empty means "no accent hint": the chrome keeps the accent preference.
    QString accent;

    friend bool operator==(const TerminalTheme &, const TerminalTheme &) = default;
};

enum class TerminalThemeImportError : std::uint8_t
{
    unreadable,
    unsupportedFormat,
    invalidColor,
    writeFailed,
};

[[nodiscard]] QString hexColor(terminal::TerminalColor color);
[[nodiscard]] std::optional<terminal::TerminalColor> parseHexColor(QStringView text);
[[nodiscard]] bool darkBackground(terminal::TerminalColor color) noexcept;
[[nodiscard]] QString terminalThemeSlug(const QString &name);

[[nodiscard]] QJsonObject terminalThemeToJson(const TerminalTheme &theme);
[[nodiscard]] std::optional<TerminalTheme> terminalThemeFromJson(const QJsonObject &object);
// Windows Terminal color scheme objects (a single scheme or a settings.json
// with a "schemes" array) and Ghostty theme files (key = value lines).
[[nodiscard]] std::expected<std::vector<TerminalTheme>, TerminalThemeImportError>
parseWindowsTerminalThemes(const QByteArray &json);
[[nodiscard]] std::expected<TerminalTheme, TerminalThemeImportError> parseGhosttyTheme(const QByteArray &text,
                                                                                       const QString &name);

class TerminalThemeCatalog final
{
public:
    static constexpr auto defaultDarkThemeId = "ztermy-dark";
    static constexpr auto defaultLightThemeId = "ztermy-light";

    explicit TerminalThemeCatalog(QString directory);

    [[nodiscard]] static const std::vector<TerminalTheme> &builtInThemes();
    [[nodiscard]] const QString &directory() const noexcept { return m_directory; }
    [[nodiscard]] const std::vector<TerminalTheme> &themes() const noexcept { return m_themes; }
    [[nodiscard]] std::optional<TerminalTheme> find(const QString &id) const;
    // Resolves a persisted id to a theme, falling back to the built-in default
    // for the requested darkness when the id is unknown.
    [[nodiscard]] TerminalTheme resolve(const QString &id, bool preferDark) const;

    void reload();
    // Detects the file format, stores the theme(s) as ztermy JSON under the
    // catalog directory and returns their ids in catalog order.
    [[nodiscard]] std::expected<std::vector<QString>, TerminalThemeImportError> importFile(const QString &path);
    [[nodiscard]] bool remove(const QString &id);

private:
    [[nodiscard]] QString uniqueId(const QString &slug) const;
    [[nodiscard]] bool store(TerminalTheme &theme) const;

    QString m_directory;
    std::vector<TerminalTheme> m_themes;
};

} // namespace ztermy::config
