#include "core/config/ApplicationSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <optional>
#include <utility>

namespace
{

constexpr qint64 maximumSettingsFileSize = 64 * 1024;
constexpr qint64 currentSchemaVersion = 1;

using ztermy::config::ApplicationSettings;
using ztermy::config::ApplicationSettingsStoreError;
using ztermy::config::BackdropPreference;
using ztermy::config::CursorPreference;
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
    if (token == QStringLiteral("none"))
    {
        return BackdropPreference::none;
    }
    if (token == QStringLiteral("mica"))
    {
        return BackdropPreference::mica;
    }
    if (token == QStringLiteral("acrylic"))
    {
        return BackdropPreference::acrylic;
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

[[nodiscard]] bool validSettings(const ApplicationSettings &settings)
{
    const QString fontFamily = settings.terminalFontFamily.trimmed();
    return settings.windowOpacity >= 0.5 && settings.windowOpacity <= 1.0 && !fontFamily.isEmpty()
           && fontFamily.size() <= 128 && settings.terminalFontSize >= 8 && settings.terminalFontSize <= 32;
}

[[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError> parseSettings(const QJsonObject &root)
{
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble() || versionValue.toInteger() != currentSchemaVersion)
    {
        return std::unexpected(versionValue.isDouble() ? ApplicationSettingsStoreError::unsupportedVersion
                                                       : ApplicationSettingsStoreError::invalidFormat);
    }

    const QJsonValue themeValue = root.value(QStringLiteral("theme"));
    const QJsonValue opacityValue = root.value(QStringLiteral("windowOpacity"));
    const QJsonValue backdropValue = root.value(QStringLiteral("backdrop"));
    const QJsonValue fontFamilyValue = root.value(QStringLiteral("terminalFontFamily"));
    const QJsonValue fontSizeValue = root.value(QStringLiteral("terminalFontSize"));
    const QJsonValue cursorValue = root.value(QStringLiteral("cursor"));
    const QJsonValue cursorBlinkValue = root.value(QStringLiteral("cursorBlink"));
    const QJsonValue copyOnSelectValue = root.value(QStringLiteral("copyOnSelect"));
    const QJsonValue confirmMultilinePasteValue = root.value(QStringLiteral("confirmMultilinePaste"));

    if (!themeValue.isString() || !opacityValue.isDouble() || !backdropValue.isString() || !fontFamilyValue.isString()
        || !fontSizeValue.isDouble() || !cursorValue.isString() || !cursorBlinkValue.isBool()
        || !copyOnSelectValue.isBool() || !confirmMultilinePasteValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    const auto theme = parsePreference<ThemePreference>(themeValue.toString());
    const auto backdrop = parsePreference<BackdropPreference>(backdropValue.toString());
    const auto cursor = parsePreference<CursorPreference>(cursorValue.toString());
    const qint64 fontSize = fontSizeValue.toInteger(-1);
    if (!theme || !backdrop || !cursor || fontSizeValue.toDouble() != static_cast<double>(fontSize))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    ApplicationSettings settings{
        .theme = *theme,
        .windowOpacity = opacityValue.toDouble(),
        .backdrop = *backdrop,
        .terminalFontFamily = fontFamilyValue.toString(),
        .terminalFontSize = static_cast<int>(fontSize),
        .cursor = *cursor,
        .cursorBlink = cursorBlinkValue.toBool(),
        .copyOnSelect = copyOnSelectValue.toBool(),
        .confirmMultilinePaste = confirmMultilinePasteValue.toBool(),
    };
    if (!validSettings(settings))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    settings.terminalFontFamily = settings.terminalFontFamily.trimmed();
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
        {QStringLiteral("windowOpacity"), settings.windowOpacity},
        {QStringLiteral("backdrop"), backdropPreferenceToken(settings.backdrop)},
        {QStringLiteral("terminalFontFamily"), settings.terminalFontFamily.trimmed()},
        {QStringLiteral("terminalFontSize"), settings.terminalFontSize},
        {QStringLiteral("cursor"), cursorPreferenceToken(settings.cursor)},
        {QStringLiteral("cursorBlink"), settings.cursorBlink},
        {QStringLiteral("copyOnSelect"), settings.copyOnSelect},
        {QStringLiteral("confirmMultilinePaste"), settings.confirmMultilinePaste},
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
        case BackdropPreference::mica:
            return QStringLiteral("mica");
        case BackdropPreference::acrylic:
            return QStringLiteral("acrylic");
        case BackdropPreference::none:
        default:
            return QStringLiteral("none");
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

std::optional<ThemePreference> parseThemePreference(const QString &token)
{
    return parsePreference<ThemePreference>(token);
}

std::optional<BackdropPreference> parseBackdropPreference(const QString &token)
{
    return parsePreference<BackdropPreference>(token);
}

std::optional<CursorPreference> parseCursorPreference(const QString &token)
{
    return parsePreference<CursorPreference>(token);
}

} // namespace ztermy::config
