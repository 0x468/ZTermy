#include "core/config/ApplicationSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <optional>
#include <utility>

namespace
{

constexpr qint64 maximumSettingsFileSize = qint64{64} * 1024;
constexpr qint64 legacySchemaVersion = 1;
constexpr qint64 materialSchemaVersion = 2;
constexpr qint64 terminalAppearanceSchemaVersion = 3;
constexpr qint64 accentSchemaVersion = 4;
constexpr qint64 credentialStorageSchemaVersion = 5;
constexpr qint64 localizationSchemaVersion = 6;
constexpr qint64 currentSchemaVersion = 7;

using ztermy::config::AccentPreference;
using ztermy::config::ApplicationSettings;
using ztermy::config::ApplicationSettingsStoreError;
using ztermy::config::BackdropPreference;
using ztermy::config::CredentialStoragePreference;
using ztermy::config::CursorPreference;
using ztermy::config::LanguagePreference;
using ztermy::config::ThemePreference;

template <typename Preference>
[[nodiscard]] std::optional<Preference> parsePreference(const QString &token);

template <>
[[nodiscard]] std::optional<ThemePreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("system"))
    {
        return ThemePreference::system;
    }
    if (token == QStringLiteral("dark"))
    {
        return ThemePreference::dark;
    }
    if (token == QStringLiteral("light"))
    {
        return ThemePreference::light;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<BackdropPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("acrylic"))
    {
        return BackdropPreference::acrylic;
    }
    if (token == QStringLiteral("transparent") || token == QStringLiteral("none"))
    {
        return BackdropPreference::transparent;
    }
    if (token == QStringLiteral("mica"))
    {
        return BackdropPreference::mica;
    }
    if (token == QStringLiteral("micaAlt"))
    {
        return BackdropPreference::micaAlt;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<AccentPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("ztermy"))
    {
        return AccentPreference::ztermy;
    }
    if (token == QStringLiteral("system"))
    {
        return AccentPreference::system;
    }
    if (token == QStringLiteral("custom"))
    {
        return AccentPreference::custom;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<CursorPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("terminal"))
    {
        return CursorPreference::terminal;
    }
    if (token == QStringLiteral("block"))
    {
        return CursorPreference::block;
    }
    if (token == QStringLiteral("bar"))
    {
        return CursorPreference::bar;
    }
    if (token == QStringLiteral("underline"))
    {
        return CursorPreference::underline;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<CredentialStoragePreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("automatic"))
    {
        return CredentialStoragePreference::automatic;
    }
    if (token == QStringLiteral("system"))
    {
        return CredentialStoragePreference::system;
    }
    if (token == QStringLiteral("portable"))
    {
        return CredentialStoragePreference::portable;
    }
    if (token == QStringLiteral("session"))
    {
        return CredentialStoragePreference::session;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<LanguagePreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("system"))
    {
        return LanguagePreference::system;
    }
    if (token == QStringLiteral("en"))
    {
        return LanguagePreference::english;
    }
    if (token == QStringLiteral("zh_CN"))
    {
        return LanguagePreference::simplifiedChinese;
    }
    return std::nullopt;
}

[[nodiscard]] bool validSettings(const ApplicationSettings &settings)
{
    const QString fontFamily = settings.terminalFontFamily.trimmed();
    const QString uiFontFamily = settings.uiFontFamily.trimmed();
    const QString customAccent = settings.customAccent.trimmed();
    const bool validCustomAccent = customAccent.size() == 7 && customAccent.front() == QLatin1Char('#')
                                   && std::ranges::all_of(customAccent.sliced(1), [](const QChar character) {
                                          const ushort value = character.unicode();
                                          return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F')
                                                 || (value >= 'a' && value <= 'f');
                                      });
    return settings.backdropOpacity >= 0.0 && settings.backdropOpacity <= 1.0
           && settings.terminalBackgroundOpacity >= 0.0 && settings.terminalBackgroundOpacity <= 1.0
           && uiFontFamily.size() <= 128 && !fontFamily.isEmpty() && fontFamily.size() <= 128
           && settings.terminalFontSize >= 8 && settings.terminalFontSize <= 32 && validCustomAccent;
}

[[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError> parseSettings(const QJsonObject &root)
{
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    const qint64 version = versionValue.toInteger();
    if (version != legacySchemaVersion && version != materialSchemaVersion && version != terminalAppearanceSchemaVersion
        && version != accentSchemaVersion && version != credentialStorageSchemaVersion
        && version != localizationSchemaVersion && version != currentSchemaVersion)
    {
        return std::unexpected(ApplicationSettingsStoreError::unsupportedVersion);
    }

    const QJsonValue themeValue = root.value(QStringLiteral("theme"));
    const QJsonValue opacityValue = root.value(version == legacySchemaVersion ? QStringLiteral("windowOpacity")
                                                                              : QStringLiteral("backdropOpacity"));
    const QJsonValue backdropValue = root.value(QStringLiteral("backdrop"));
    const QJsonValue accentValue = root.value(QStringLiteral("accent"));
    const QJsonValue customAccentValue = root.value(QStringLiteral("customAccent"));
    const QJsonValue uiFontFamilyValue = root.value(QStringLiteral("uiFontFamily"));
    const QJsonValue fontFamilyValue = root.value(QStringLiteral("terminalFontFamily"));
    const QJsonValue fontSizeValue = root.value(QStringLiteral("terminalFontSize"));
    const QJsonValue showAllTerminalFontsValue = root.value(QStringLiteral("showAllTerminalFonts"));
    const QJsonValue terminalLigaturesValue = root.value(QStringLiteral("terminalLigatures"));
    const QJsonValue terminalBackgroundOpacityValue = root.value(QStringLiteral("terminalBackgroundOpacity"));
    const QJsonValue cursorValue = root.value(QStringLiteral("cursor"));
    const QJsonValue cursorBlinkValue = root.value(QStringLiteral("cursorBlink"));
    const QJsonValue copyOnSelectValue = root.value(QStringLiteral("copyOnSelect"));
    const QJsonValue confirmMultilinePasteValue = root.value(QStringLiteral("confirmMultilinePaste"));
    const QJsonValue credentialStorageValue = root.value(QStringLiteral("credentialStorage"));
    const QJsonValue languageValue = root.value(QStringLiteral("language"));

    if (!themeValue.isString() || !opacityValue.isDouble() || !backdropValue.isString() || !fontFamilyValue.isString()
        || !fontSizeValue.isDouble() || !cursorValue.isString() || !cursorBlinkValue.isBool()
        || !copyOnSelectValue.isBool() || !confirmMultilinePasteValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= terminalAppearanceSchemaVersion && !terminalBackgroundOpacityValue.isDouble())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= accentSchemaVersion && (!accentValue.isString() || !customAccentValue.isString()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= credentialStorageSchemaVersion && !credentialStorageValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= localizationSchemaVersion && !languageValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version == currentSchemaVersion
        && (!uiFontFamilyValue.isString() || !showAllTerminalFontsValue.isBool() || !terminalLigaturesValue.isBool()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    const auto theme = parsePreference<ThemePreference>(themeValue.toString());
    const auto backdrop = parsePreference<BackdropPreference>(backdropValue.toString());
    const auto accent = version >= accentSchemaVersion ? parsePreference<AccentPreference>(accentValue.toString())
                                                       : std::optional{AccentPreference::ztermy};
    const auto cursor = parsePreference<CursorPreference>(cursorValue.toString());
    const auto credentialStorage = version >= credentialStorageSchemaVersion
                                       ? parsePreference<CredentialStoragePreference>(credentialStorageValue.toString())
                                       : std::optional{CredentialStoragePreference::automatic};
    const auto language = version >= localizationSchemaVersion
                              ? parsePreference<LanguagePreference>(languageValue.toString())
                              : std::optional{LanguagePreference::system};
    const qint64 fontSize = fontSizeValue.toInteger(-1);
    if (!theme || !backdrop || !accent || !cursor || !credentialStorage || !language
        || fontSizeValue.toDouble() != static_cast<double>(fontSize))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    ApplicationSettings settings{
        .theme = *theme,
        .backdropOpacity = opacityValue.toDouble(),
        .backdrop = *backdrop,
        .accent = *accent,
        .customAccent = version >= accentSchemaVersion ? customAccentValue.toString() : QStringLiteral("#22C55E"),
        .uiFontFamily = version == currentSchemaVersion ? uiFontFamilyValue.toString() : QString{},
        .terminalFontFamily = fontFamilyValue.toString(),
        .terminalFontSize = static_cast<int>(fontSize),
        .showAllTerminalFonts = version == currentSchemaVersion && showAllTerminalFontsValue.toBool(),
        .terminalLigatures = version != currentSchemaVersion || terminalLigaturesValue.toBool(),
        .terminalBackgroundOpacity =
            version >= terminalAppearanceSchemaVersion ? terminalBackgroundOpacityValue.toDouble() : 1.0,
        .cursor = *cursor,
        .cursorBlink = cursorBlinkValue.toBool(),
        .copyOnSelect = copyOnSelectValue.toBool(),
        .confirmMultilinePaste = confirmMultilinePasteValue.toBool(),
        .credentialStorage = *credentialStorage,
        .language = *language,
    };
    if (!validSettings(settings))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    settings.terminalFontFamily = settings.terminalFontFamily.trimmed();
    settings.uiFontFamily = settings.uiFontFamily.trimmed();
    settings.customAccent = settings.customAccent.trimmed().toUpper();
    return settings;
}

} // namespace

namespace ztermy::config
{

ApplicationSettingsStore::ApplicationSettingsStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &ApplicationSettingsStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<ApplicationSettings, ApplicationSettingsStoreError> ApplicationSettingsStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidPath);
    }

    QFile file(m_filePath);
    if (!file.exists())
    {
        return ApplicationSettings{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumSettingsFileSize)
    {
        return std::unexpected(file.size() > maximumSettingsFileSize ? ApplicationSettingsStoreError::invalidFormat
                                                                     : ApplicationSettingsStoreError::ioError);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    return parseSettings(document.object());
}

std::expected<void, ApplicationSettingsStoreError>
ApplicationSettingsStore::save(const ApplicationSettings &settings) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidPath);
    }
    if (!validSettings(settings))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    const QFileInfo fileInfo(m_filePath);
    if (!QDir().mkpath(fileInfo.absolutePath()))
    {
        return std::unexpected(ApplicationSettingsStoreError::ioError);
    }

    const QJsonObject root{
        {QStringLiteral("version"), currentSchemaVersion},
        {QStringLiteral("theme"), themePreferenceToken(settings.theme)},
        {QStringLiteral("backdropOpacity"), settings.backdropOpacity},
        {QStringLiteral("backdrop"), backdropPreferenceToken(settings.backdrop)},
        {QStringLiteral("accent"), accentPreferenceToken(settings.accent)},
        {QStringLiteral("customAccent"), settings.customAccent.trimmed().toUpper()},
        {QStringLiteral("uiFontFamily"), settings.uiFontFamily.trimmed()},
        {QStringLiteral("terminalFontFamily"), settings.terminalFontFamily.trimmed()},
        {QStringLiteral("terminalFontSize"), settings.terminalFontSize},
        {QStringLiteral("showAllTerminalFonts"), settings.showAllTerminalFonts},
        {QStringLiteral("terminalLigatures"), settings.terminalLigatures},
        {QStringLiteral("terminalBackgroundOpacity"), settings.terminalBackgroundOpacity},
        {QStringLiteral("cursor"), cursorPreferenceToken(settings.cursor)},
        {QStringLiteral("cursorBlink"), settings.cursorBlink},
        {QStringLiteral("copyOnSelect"), settings.copyOnSelect},
        {QStringLiteral("confirmMultilinePaste"), settings.confirmMultilinePaste},
        {QStringLiteral("credentialStorage"), credentialStoragePreferenceToken(settings.credentialStorage)},
        {QStringLiteral("language"), languagePreferenceToken(settings.language)},
    };

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return std::unexpected(ApplicationSettingsStoreError::ioError);
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        return std::unexpected(ApplicationSettingsStoreError::ioError);
    }
    return {};
}

QString themePreferenceToken(const ThemePreference preference)
{
    switch (preference)
    {
        case ThemePreference::system:
            return QStringLiteral("system");
        case ThemePreference::light:
            return QStringLiteral("light");
        case ThemePreference::dark:
        default:
            return QStringLiteral("dark");
    }
}

QString backdropPreferenceToken(const BackdropPreference preference)
{
    switch (preference)
    {
        case BackdropPreference::acrylic:
            return QStringLiteral("acrylic");
        case BackdropPreference::transparent:
            return QStringLiteral("transparent");
        case BackdropPreference::mica:
            return QStringLiteral("mica");
        case BackdropPreference::micaAlt:
            return QStringLiteral("micaAlt");
        default:
            return QStringLiteral("acrylic");
    }
}

QString accentPreferenceToken(const AccentPreference preference)
{
    switch (preference)
    {
        case AccentPreference::system:
            return QStringLiteral("system");
        case AccentPreference::custom:
            return QStringLiteral("custom");
        case AccentPreference::ztermy:
        default:
            return QStringLiteral("ztermy");
    }
}

QString cursorPreferenceToken(const CursorPreference preference)
{
    switch (preference)
    {
        case CursorPreference::block:
            return QStringLiteral("block");
        case CursorPreference::bar:
            return QStringLiteral("bar");
        case CursorPreference::underline:
            return QStringLiteral("underline");
        case CursorPreference::terminal:
        default:
            return QStringLiteral("terminal");
    }
}

QString credentialStoragePreferenceToken(const CredentialStoragePreference preference)
{
    switch (preference)
    {
        case CredentialStoragePreference::system:
            return QStringLiteral("system");
        case CredentialStoragePreference::portable:
            return QStringLiteral("portable");
        case CredentialStoragePreference::session:
            return QStringLiteral("session");
        case CredentialStoragePreference::automatic:
        default:
            return QStringLiteral("automatic");
    }
}

QString languagePreferenceToken(const LanguagePreference preference)
{
    switch (preference)
    {
        case LanguagePreference::english:
            return QStringLiteral("en");
        case LanguagePreference::simplifiedChinese:
            return QStringLiteral("zh_CN");
        case LanguagePreference::system:
        default:
            return QStringLiteral("system");
    }
}

std::optional<ThemePreference> parseThemePreference(const QString &token)
{
    return parsePreference<ThemePreference>(token);
}

std::optional<BackdropPreference> parseBackdropPreference(const QString &token)
{
    return parsePreference<BackdropPreference>(token);
}

std::optional<AccentPreference> parseAccentPreference(const QString &token)
{
    return parsePreference<AccentPreference>(token);
}

std::optional<CursorPreference> parseCursorPreference(const QString &token)
{
    return parsePreference<CursorPreference>(token);
}

std::optional<CredentialStoragePreference> parseCredentialStoragePreference(const QString &token)
{
    return parsePreference<CredentialStoragePreference>(token);
}

std::optional<LanguagePreference> parseLanguagePreference(const QString &token)
{
    return parsePreference<LanguagePreference>(token);
}

} // namespace ztermy::config
