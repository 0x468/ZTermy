#pragma once

#include <QString>

#include <cstdint>
#include <expected>
#include <optional>

namespace ztermy::config
{

enum class ThemePreference : std::uint8_t
{
    system,
    dark,
    light,
};

enum class BackdropPreference : std::uint8_t
{
    acrylic,
    transparent,
    mica,
    micaAlt,
};

enum class AccentPreference : std::uint8_t
{
    ztermy,
    system,
    custom,
};

enum class CursorPreference : std::uint8_t
{
    terminal,
    block,
    bar,
    underline,
};

struct ApplicationSettings final
{
    ThemePreference theme = ThemePreference::dark;
    double backdropOpacity = 1.0;
    BackdropPreference backdrop = BackdropPreference::acrylic;
    AccentPreference accent = AccentPreference::ztermy;
    QString customAccent = QStringLiteral("#22C55E");
    QString terminalFontFamily = QStringLiteral("Cascadia Mono");
    int terminalFontSize = 14;
    double terminalBackgroundOpacity = 1.0;
    CursorPreference cursor = CursorPreference::terminal;
    bool cursorBlink = true;
    bool copyOnSelect = false;
    bool confirmMultilinePaste = true;

    [[nodiscard]] friend bool operator==(const ApplicationSettings &, const ApplicationSettings &) = default;
};

enum class ApplicationSettingsStoreError : std::uint8_t
{
    invalidPath,
    ioError,
    invalidFormat,
    unsupportedVersion,
};

class ApplicationSettingsStore final
{
public:
    explicit ApplicationSettingsStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError> load() const;
    [[nodiscard]] std::expected<void, ApplicationSettingsStoreError> save(const ApplicationSettings &settings) const;

private:
    QString m_filePath;
};

[[nodiscard]] QString themePreferenceToken(ThemePreference preference);
[[nodiscard]] QString backdropPreferenceToken(BackdropPreference preference);
[[nodiscard]] QString accentPreferenceToken(AccentPreference preference);
[[nodiscard]] QString cursorPreferenceToken(CursorPreference preference);
[[nodiscard]] std::optional<ThemePreference> parseThemePreference(const QString &token);
[[nodiscard]] std::optional<BackdropPreference> parseBackdropPreference(const QString &token);
[[nodiscard]] std::optional<AccentPreference> parseAccentPreference(const QString &token);
[[nodiscard]] std::optional<CursorPreference> parseCursorPreference(const QString &token);

} // namespace ztermy::config
