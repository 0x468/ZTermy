#include "core/config/TerminalThemeCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <iterator>

namespace ztermy::config
{

namespace
{

using terminal::TerminalColor;
using terminal::TerminalColorScheme;

constexpr std::array<const char *, 16> ansiKeys = {
    "black",      "red",           "green",       "yellow",      "blue",        "magenta",
    "cyan",       "white",         "brightBlack", "brightRed",   "brightGreen", "brightYellow",
    "brightBlue", "brightMagenta", "brightCyan",  "brightWhite",
};

constexpr std::array<const char *, 16> windowsTerminalAnsiKeys = {
    "black",      "red",          "green",       "yellow",      "blue",        "purple",
    "cyan",       "white",        "brightBlack", "brightRed",   "brightGreen", "brightYellow",
    "brightBlue", "brightPurple", "brightCyan",  "brightWhite",
};

[[nodiscard]] TerminalColor rgb(const std::uint32_t value) noexcept
{
    return {.red = static_cast<std::uint8_t>((value >> 16) & 0xFF),
            .green = static_cast<std::uint8_t>((value >> 8) & 0xFF),
            .blue = static_cast<std::uint8_t>(value & 0xFF)};
}

[[nodiscard]] std::array<TerminalColor, 16> ansi(const std::array<std::uint32_t, 16> &values)
{
    std::array<TerminalColor, 16> colors{};
    std::ranges::transform(values, colors.begin(), rgb);
    return colors;
}

struct BuiltInSpec final
{
    const char *id;
    const char *name;
    bool dark;
    std::uint32_t background;
    std::uint32_t foreground;
    std::uint32_t cursor;
    std::uint32_t selection;
    std::uint32_t selectionText;
    const char *accent;
    std::array<std::uint32_t, 16> ansi;
};

// Values are the published palettes of each scheme; only the two ztermy
// entries are original.
constexpr std::array<BuiltInSpec, 10> builtInSpecs = {{
    {"ztermy-dark",
     "ztermy Dark",
     true,
     0x0B1017,
     0xF8FAFC,
     0xA78BFA,
     0x2A5B91,
     0xFFFFFF,
     "",
     {0x1E293B, 0xF87171, 0x4ADE80, 0xFBBF24, 0x60A5FA, 0xC084FC, 0x22D3EE, 0xCBD5E1, 0x475569, 0xFCA5A5, 0x86EFAC,
      0xFDE68A, 0x93C5FD, 0xD8B4FE, 0x67E8F9, 0xF8FAFC}},
    {"ztermy-light",
     "ztermy Light",
     false,
     0xFFFFFF,
     0x0F172A,
     0x7C3AED,
     0xBFDBFE,
     0x0F172A,
     "",
     {0x0F172A, 0xB91C1C, 0x15803D, 0xB45309, 0x1D4ED8, 0x7E22CE, 0x0E7490, 0x94A3B8, 0x475569, 0xDC2626, 0x16A34A,
      0xD97706, 0x2563EB, 0x9333EA, 0x0891B2, 0xE2E8F0}},
    {"one-dark",
     "One Dark",
     true,
     0x282C34,
     0xABB2BF,
     0x528BFF,
     0x3E4451,
     0xFFFFFF,
     "#61AFEF",
     {0x282C34, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xABB2BF, 0x5C6370, 0xE06C75, 0x98C379,
      0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xFFFFFF}},
    {"dracula",
     "Dracula",
     true,
     0x282A36,
     0xF8F8F2,
     0xF8F8F2,
     0x44475A,
     0xF8F8F2,
     "#BD93F9",
     {0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2, 0x6272A4, 0xFF6E6E, 0x69FF94,
      0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF}},
    {"solarized-dark",
     "Solarized Dark",
     true,
     0x002B36,
     0x839496,
     0x93A1A1,
     0x073642,
     0x93A1A1,
     "#268BD2",
     {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5, 0x002B36, 0xCB4B16, 0x586E75,
      0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
    {"solarized-light",
     "Solarized Light",
     false,
     0xFDF6E3,
     0x657B83,
     0x586E75,
     0xEEE8D5,
     0x586E75,
     "#268BD2",
     {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5, 0x002B36, 0xCB4B16, 0x586E75,
      0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
    {"gruvbox-dark",
     "Gruvbox Dark",
     true,
     0x282828,
     0xEBDBB2,
     0xEBDBB2,
     0x504945,
     0xEBDBB2,
     "#D79921",
     {0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984, 0x928374, 0xFB4934, 0xB8BB26,
      0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2}},
    {"nord",
     "Nord",
     true,
     0x2E3440,
     0xD8DEE9,
     0xD8DEE9,
     0x434C5E,
     0xECEFF4,
     "#88C0D0",
     {0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0, 0x4C566A, 0xBF616A, 0xA3BE8C,
      0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4}},
    {"catppuccin-mocha",
     "Catppuccin Mocha",
     true,
     0x1E1E2E,
     0xCDD6F4,
     0xF5E0DC,
     0x45475A,
     0xCDD6F4,
     "#CBA6F7",
     {0x45475A, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xBAC2DE, 0x585B70, 0xF38BA8, 0xA6E3A1,
      0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xA6ADC8}},
    {"tokyo-night",
     "Tokyo Night",
     true,
     0x1A1B26,
     0xC0CAF5,
     0xC0CAF5,
     0x33467C,
     0xC0CAF5,
     "#7AA2F7",
     {0x15161E, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xA9B1D6, 0x414868, 0xF7768E, 0x9ECE6A,
      0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5}},
}};

[[nodiscard]] TerminalTheme builtInTheme(const BuiltInSpec &spec)
{
    return {.id = QString::fromLatin1(spec.id),
            .name = QString::fromLatin1(spec.name),
            .dark = spec.dark,
            .builtIn = true,
            .scheme = {.foreground = rgb(spec.foreground),
                       .background = rgb(spec.background),
                       .cursor = rgb(spec.cursor),
                       .ansi = ansi(spec.ansi)},
            .selectionBackground = rgb(spec.selection),
            .selectionForeground = rgb(spec.selectionText),
            .accent = QString::fromLatin1(spec.accent)};
}

[[nodiscard]] std::optional<TerminalColor> colorValue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? parseHexColor(value.toString()) : std::nullopt;
}

[[nodiscard]] std::optional<TerminalColor> colorValue(const QJsonObject &object, const char *key,
                                                      const TerminalColor fallback)
{
    return object.contains(QLatin1String(key)) ? colorValue(object, key) : std::optional{fallback};
}

[[nodiscard]] std::optional<TerminalTheme> windowsTerminalTheme(const QJsonObject &scheme)
{
    const QString name = scheme.value(QStringLiteral("name")).toString().trimmed();
    const auto background = colorValue(scheme, "background");
    const auto foreground = colorValue(scheme, "foreground");
    if (name.isEmpty() || !background || !foreground)
    {
        return std::nullopt;
    }
    TerminalTheme theme{.id = terminalThemeSlug(name),
                        .name = name,
                        .dark = darkBackground(*background),
                        .scheme = {.foreground = *foreground, .background = *background},
                        .accent = {}};
    const auto cursor = colorValue(scheme, "cursorColor", *foreground);
    const auto selection = colorValue(scheme, "selectionBackground", theme.selectionBackground);
    if (!cursor || !selection)
    {
        return std::nullopt;
    }
    theme.scheme.cursor = *cursor;
    theme.selectionBackground = *selection;
    theme.selectionForeground = darkBackground(*selection) ? rgb(0xFFFFFF) : rgb(0x0F172A);
    for (std::size_t index = 0; index < windowsTerminalAnsiKeys.size(); ++index)
    {
        const auto color = colorValue(scheme, windowsTerminalAnsiKeys[index]);
        if (!color)
        {
            return std::nullopt;
        }
        theme.scheme.ansi[index] = *color;
    }
    return theme;
}

} // namespace

QString hexColor(const TerminalColor color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.red, 2, 16, QLatin1Char('0'))
        .arg(color.green, 2, 16, QLatin1Char('0'))
        .arg(color.blue, 2, 16, QLatin1Char('0'))
        .toUpper();
}

std::optional<TerminalColor> parseHexColor(QStringView text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('#')))
    {
        text = text.mid(1);
    }
    if (text.size() != 6)
    {
        return std::nullopt;
    }
    bool ok = false;
    const std::uint32_t value = text.toUInt(&ok, 16);
    return ok ? std::optional{rgb(value)} : std::nullopt;
}

bool darkBackground(const TerminalColor color) noexcept
{
    return (0.2126 * color.red) + (0.7152 * color.green) + (0.0722 * color.blue) < 128.0;
}

QString terminalThemeSlug(const QString &name)
{
    QString slug;
    bool pendingDash = false;
    for (const QChar character : name.toLower())
    {
        if (character.isLetterOrNumber() && character.unicode() < 128)
        {
            if (pendingDash && !slug.isEmpty())
            {
                slug.append(QLatin1Char('-'));
            }
            slug.append(character);
            pendingDash = false;
        }
        else
        {
            pendingDash = true;
        }
    }
    return slug.isEmpty() ? QStringLiteral("theme") : slug;
}

QJsonObject terminalThemeToJson(const TerminalTheme &theme)
{
    QJsonObject colors;
    for (std::size_t index = 0; index < ansiKeys.size(); ++index)
    {
        colors.insert(QLatin1String(ansiKeys[index]), hexColor(theme.scheme.ansi[index]));
    }
    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("id"), theme.id},
        {QStringLiteral("name"), theme.name},
        {QStringLiteral("dark"), theme.dark},
        {QStringLiteral("background"), hexColor(theme.scheme.background)},
        {QStringLiteral("foreground"), hexColor(theme.scheme.foreground)},
        {QStringLiteral("cursor"), hexColor(theme.scheme.cursor)},
        {QStringLiteral("selectionBackground"), hexColor(theme.selectionBackground)},
        {QStringLiteral("selectionForeground"), hexColor(theme.selectionForeground)},
        {QStringLiteral("accent"), theme.accent},
        {QStringLiteral("ansi"), colors},
    };
}

std::optional<TerminalTheme> terminalThemeFromJson(const QJsonObject &object)
{
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    const auto background = colorValue(object, "background");
    const auto foreground = colorValue(object, "foreground");
    const QJsonValue ansiValue = object.value(QStringLiteral("ansi"));
    if (id.isEmpty() || name.isEmpty() || !background || !foreground || !ansiValue.isObject()
        || id != terminalThemeSlug(id))
    {
        return std::nullopt;
    }
    TerminalTheme theme{.id = id,
                        .name = name,
                        .dark = object.value(QStringLiteral("dark")).toBool(darkBackground(*background)),
                        .scheme = {.foreground = *foreground, .background = *background},
                        .accent = {}};
    const auto cursor = colorValue(object, "cursor", *foreground);
    const auto selectionBackground = colorValue(object, "selectionBackground", theme.selectionBackground);
    const auto selectionForeground = colorValue(object, "selectionForeground", theme.selectionForeground);
    if (!cursor || !selectionBackground || !selectionForeground)
    {
        return std::nullopt;
    }
    theme.scheme.cursor = *cursor;
    theme.selectionBackground = *selectionBackground;
    theme.selectionForeground = *selectionForeground;
    const QString accent = object.value(QStringLiteral("accent")).toString().trimmed();
    if (!accent.isEmpty() && !parseHexColor(accent))
    {
        return std::nullopt;
    }
    theme.accent = accent.isEmpty() ? QString{} : hexColor(*parseHexColor(accent));
    const QJsonObject colors = ansiValue.toObject();
    for (std::size_t index = 0; index < ansiKeys.size(); ++index)
    {
        const auto color = colorValue(colors, ansiKeys[index]);
        if (!color)
        {
            return std::nullopt;
        }
        theme.scheme.ansi[index] = *color;
    }
    return theme;
}

std::expected<std::vector<TerminalTheme>, TerminalThemeImportError> parseWindowsTerminalThemes(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject())
    {
        return std::unexpected(TerminalThemeImportError::unsupportedFormat);
    }
    const QJsonObject root = document.object();
    QJsonArray schemes;
    if (root.value(QStringLiteral("schemes")).isArray())
    {
        schemes = root.value(QStringLiteral("schemes")).toArray();
    }
    else
    {
        schemes.append(root);
    }
    std::vector<TerminalTheme> themes;
    for (const auto &value : std::as_const(schemes))
    {
        if (!value.isObject())
        {
            return std::unexpected(TerminalThemeImportError::unsupportedFormat);
        }
        auto theme = windowsTerminalTheme(value.toObject());
        if (!theme)
        {
            return std::unexpected(TerminalThemeImportError::invalidColor);
        }
        themes.push_back(std::move(*theme));
    }
    if (themes.empty())
    {
        return std::unexpected(TerminalThemeImportError::unsupportedFormat);
    }
    return themes;
}

std::expected<TerminalTheme, TerminalThemeImportError> parseGhosttyTheme(const QByteArray &text, const QString &name)
{
    TerminalTheme theme{.id = terminalThemeSlug(name), .name = name.trimmed(), .scheme = {}};
    bool background = false;
    bool foreground = false;
    std::array<bool, 16> paletteSeen{};
    std::optional<TerminalColor> cursor;
    std::optional<TerminalColor> selectionBackground;
    std::optional<TerminalColor> selectionForeground;
    for (const QByteArray &rawLine : text.split('\n'))
    {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
        {
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
        {
            return std::unexpected(TerminalThemeImportError::unsupportedFormat);
        }
        const QString key = line.first(equals).trimmed();
        const QString value = line.sliced(equals + 1).trimmed();
        if (key == QStringLiteral("palette"))
        {
            const qsizetype separator = value.indexOf(QLatin1Char('='));
            bool ok = false;
            const int index = separator > 0 ? value.first(separator).trimmed().toInt(&ok) : -1;
            const auto color = separator > 0 ? parseHexColor(value.sliced(separator + 1)) : std::nullopt;
            if (!ok || index < 0 || index > 15 || !color)
            {
                return std::unexpected(TerminalThemeImportError::invalidColor);
            }
            theme.scheme.ansi[static_cast<std::size_t>(index)] = *color;
            paletteSeen[static_cast<std::size_t>(index)] = true;
            continue;
        }
        const auto color = parseHexColor(value);
        if (!color)
        {
            // Unknown keys and non-color values (e.g. cursor-style) are ignored.
            if (key == QStringLiteral("background") || key == QStringLiteral("foreground"))
            {
                return std::unexpected(TerminalThemeImportError::invalidColor);
            }
            continue;
        }
        if (key == QStringLiteral("background"))
        {
            theme.scheme.background = *color;
            background = true;
        }
        else if (key == QStringLiteral("foreground"))
        {
            theme.scheme.foreground = *color;
            foreground = true;
        }
        else if (key == QStringLiteral("cursor-color"))
        {
            cursor = *color;
        }
        else if (key == QStringLiteral("selection-background"))
        {
            selectionBackground = *color;
        }
        else if (key == QStringLiteral("selection-foreground"))
        {
            selectionForeground = *color;
        }
    }
    if (!background || !foreground || !std::ranges::all_of(paletteSeen, std::identity{}))
    {
        return std::unexpected(TerminalThemeImportError::unsupportedFormat);
    }
    theme.dark = darkBackground(theme.scheme.background);
    theme.scheme.cursor = cursor.value_or(theme.scheme.foreground);
    if (selectionBackground)
    {
        theme.selectionBackground = *selectionBackground;
        theme.selectionForeground =
            selectionForeground.value_or(darkBackground(*selectionBackground) ? rgb(0xFFFFFF) : rgb(0x0F172A));
    }
    return theme;
}

TerminalThemeCatalog::TerminalThemeCatalog(QString directory) : m_directory(std::move(directory))
{
    reload();
}

const std::vector<TerminalTheme> &TerminalThemeCatalog::builtInThemes()
{
    static const std::vector<TerminalTheme> themes = [] {
        std::vector<TerminalTheme> result;
        result.reserve(builtInSpecs.size());
        std::ranges::transform(builtInSpecs, std::back_inserter(result), builtInTheme);
        return result;
    }();
    return themes;
}

std::optional<TerminalTheme> TerminalThemeCatalog::find(const QString &id) const
{
    const auto match = std::ranges::find(m_themes, id, &TerminalTheme::id);
    return match == m_themes.end() ? std::nullopt : std::optional{*match};
}

TerminalTheme TerminalThemeCatalog::resolve(const QString &id, const bool preferDark) const
{
    if (auto theme = find(id))
    {
        return *theme;
    }
    return *find(QString::fromLatin1(preferDark ? defaultDarkThemeId : defaultLightThemeId));
}

void TerminalThemeCatalog::reload()
{
    m_themes = builtInThemes();
    const QDir directory(m_directory);
    if (!directory.exists())
    {
        return;
    }
    std::vector<TerminalTheme> custom;
    for (const QFileInfo &entry : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        auto theme = document.isObject() ? terminalThemeFromJson(document.object()) : std::nullopt;
        if (theme && theme->id == entry.completeBaseName() && !find(theme->id)
            && std::ranges::find(custom, theme->id, &TerminalTheme::id) == custom.end())
        {
            custom.push_back(std::move(*theme));
        }
    }
    std::ranges::sort(custom, [](const TerminalTheme &left, const TerminalTheme &right) {
        return left.name.localeAwareCompare(right.name) < 0;
    });
    m_themes.insert(m_themes.end(), custom.begin(), custom.end());
}

std::expected<std::vector<QString>, TerminalThemeImportError> TerminalThemeCatalog::importFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(TerminalThemeImportError::unreadable);
    }
    const QByteArray content = file.readAll();
    const QFileInfo info(path);
    std::vector<TerminalTheme> themes;
    if (const QJsonDocument document = QJsonDocument::fromJson(content); document.isObject())
    {
        if (auto theme = terminalThemeFromJson(document.object()))
        {
            themes.push_back(std::move(*theme));
        }
        else
        {
            auto imported = parseWindowsTerminalThemes(content);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            themes = std::move(*imported);
        }
    }
    else
    {
        auto imported = parseGhosttyTheme(content, info.completeBaseName());
        if (!imported)
        {
            return std::unexpected(imported.error());
        }
        themes.push_back(std::move(*imported));
    }
    std::vector<QString> ids;
    for (TerminalTheme &theme : themes)
    {
        theme.builtIn = false;
        theme.id = uniqueId(terminalThemeSlug(theme.id));
        if (!store(theme))
        {
            return std::unexpected(TerminalThemeImportError::writeFailed);
        }
        ids.push_back(theme.id);
        m_themes.push_back(theme);
    }
    reload();
    return ids;
}

bool TerminalThemeCatalog::remove(const QString &id)
{
    const auto theme = find(id);
    if (!theme || theme->builtIn)
    {
        return false;
    }
    if (!QFile::remove(QDir(m_directory).filePath(id + QStringLiteral(".json"))))
    {
        return false;
    }
    reload();
    return true;
}

QString TerminalThemeCatalog::uniqueId(const QString &slug) const
{
    QString candidate = slug;
    for (int suffix = 2; find(candidate).has_value(); ++suffix)
    {
        candidate = QStringLiteral("%1-%2").arg(slug).arg(suffix);
    }
    return candidate;
}

bool TerminalThemeCatalog::store(TerminalTheme &theme) const
{
    if (!QDir().mkpath(m_directory))
    {
        return false;
    }
    QSaveFile file(QDir(m_directory).filePath(theme.id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    return file.write(QJsonDocument(terminalThemeToJson(theme)).toJson(QJsonDocument::Indented)) >= 0 && file.commit();
}

} // namespace ztermy::config
