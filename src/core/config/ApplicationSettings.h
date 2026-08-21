#pragma once

#include <QMap>
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

enum class CredentialStoragePreference : std::uint8_t
{
    automatic,
    system,
    portable,
    session,
};

enum class LanguagePreference : std::uint8_t
{
    system,
    english,
    simplifiedChinese,
};

enum class AiProviderPreference : std::uint8_t
{
    openAiResponses,
    openAiChatGpt,
    anthropic,
    deepSeek,
    kimi,
    zai,
    gemini,
    openRouter,
    qwen,
    ollama,
    openAiCompatible,
};

enum class AiPermissionPreference : std::uint8_t
{
    readOnly,
    ask,
    automatic,
    yolo,
};

enum class AiReasoningPreference : std::uint8_t
{
    automatic,
    disabled,
    low,
    medium,
    high,
    maximum,
};

enum class AiProxyPreference : std::uint8_t
{
    system,
    direct,
    custom,
};

struct ApplicationSettings final
{
    ThemePreference theme = ThemePreference::dark;
    double backdropOpacity = 1.0;
    BackdropPreference backdrop = BackdropPreference::acrylic;
    AccentPreference accent = AccentPreference::ztermy;
    QString customAccent = QStringLiteral("#22C55E");
    QString uiFontFamily;
    QString terminalFontFamily = QStringLiteral("Cascadia Mono");
    int terminalFontSize = 14;
    bool showAllTerminalFonts = false;
    bool terminalLigatures = true;
    double terminalBackgroundOpacity = 1.0;
    CursorPreference cursor = CursorPreference::terminal;
    bool cursorBlink = true;
    bool copyOnSelect = false;
    bool confirmMultilinePaste = true;
    bool sftpShowHiddenFiles = false;
    bool sftpConfirmDelete = true;
    CredentialStoragePreference credentialStorage = CredentialStoragePreference::automatic;
    LanguagePreference language = LanguagePreference::system;
    QMap<QString, QString> shortcutOverrides;
    AiProviderPreference aiProvider = AiProviderPreference::openAiResponses;
    QString aiBaseUrl = QStringLiteral("https://api.openai.com/v1");
    QString aiEndpointPath;
    QString aiModel;
    QString aiCredentialReference = QStringLiteral("ai-default");
    bool aiAutomaticContext = false;
    AiPermissionPreference aiPermission = AiPermissionPreference::ask;
    bool aiConversationHistoryEnabled = true;
    bool aiDebugTraceEnabled = false;
    AiReasoningPreference aiReasoning = AiReasoningPreference::automatic;
    AiProxyPreference aiProxy = AiProxyPreference::system;
    QString aiProxyUrl;
    QString aiProxyUsername;

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
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError> load() const;
    [[nodiscard]] std::expected<void, ApplicationSettingsStoreError> save(const ApplicationSettings &settings) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

[[nodiscard]] QString themePreferenceToken(ThemePreference preference);
[[nodiscard]] QString backdropPreferenceToken(BackdropPreference preference);
[[nodiscard]] QString accentPreferenceToken(AccentPreference preference);
[[nodiscard]] QString cursorPreferenceToken(CursorPreference preference);
[[nodiscard]] QString credentialStoragePreferenceToken(CredentialStoragePreference preference);
[[nodiscard]] QString languagePreferenceToken(LanguagePreference preference);
[[nodiscard]] QString aiProviderPreferenceToken(AiProviderPreference preference);
[[nodiscard]] QString aiPermissionPreferenceToken(AiPermissionPreference preference);
[[nodiscard]] QString aiReasoningPreferenceToken(AiReasoningPreference preference);
[[nodiscard]] QString aiProxyPreferenceToken(AiProxyPreference preference);
[[nodiscard]] std::optional<ThemePreference> parseThemePreference(const QString &token);
[[nodiscard]] std::optional<BackdropPreference> parseBackdropPreference(const QString &token);
[[nodiscard]] std::optional<AccentPreference> parseAccentPreference(const QString &token);
[[nodiscard]] std::optional<CursorPreference> parseCursorPreference(const QString &token);
[[nodiscard]] std::optional<CredentialStoragePreference> parseCredentialStoragePreference(const QString &token);
[[nodiscard]] std::optional<LanguagePreference> parseLanguagePreference(const QString &token);
[[nodiscard]] std::optional<AiProviderPreference> parseAiProviderPreference(const QString &token);
[[nodiscard]] std::optional<AiPermissionPreference> parseAiPermissionPreference(const QString &token);
[[nodiscard]] std::optional<AiReasoningPreference> parseAiReasoningPreference(const QString &token);
[[nodiscard]] std::optional<AiProxyPreference> parseAiProxyPreference(const QString &token);

} // namespace ztermy::config
