#include "core/config/ApplicationSettings.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

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
constexpr qint64 fontOptionsSchemaVersion = 7;
constexpr qint64 workbenchSchemaVersion = 8;
constexpr qint64 shortcutSchemaVersion = 9;
constexpr qint64 sftpSchemaVersion = 10;
constexpr qint64 aiProviderSchemaVersion = 11;
constexpr qint64 aiPermissionSchemaVersion = 12;
constexpr qint64 aiConversationHistorySchemaVersion = 13;
constexpr qint64 aiDebugTraceSchemaVersion = 14;
constexpr qint64 aiAgentModeSchemaVersion = 15;
constexpr qint64 aiReasoningSchemaVersion = 16;
constexpr qint64 aiExplicitContextSchemaVersion = 17;
constexpr qint64 aiConversationFirstSchemaVersion = 18;
constexpr qint64 aiPermissionContractSchemaVersion = 19;
// Version 20 was briefly written by builds that exposed an external-agent
// selector. That product direction was retired, but existing user settings
// must remain readable. Keep the schema number reserved and ignore the
// removed `aiAgent` member when loading it.
constexpr qint64 retiredExternalAgentSchemaVersion = 20;
// Version 21 adds the native ChatGPT subscription provider token. Older
// schemas continue to load with their existing provider unchanged.
constexpr qint64 nativeChatGptProviderSchemaVersion = 21;
// Version 22 adds an AI-only proxy policy. It is intentionally separate from
// SSH proxy chains and defaults to the operating-system proxy configuration.
constexpr qint64 aiProxySchemaVersion = 22;
// Version 23 adds the optional Windows close-to-tray application behavior.
constexpr qint64 closeToTraySchemaVersion = 23;
// Version 24 adds a restart-scoped performance mode. The mode preserves the
// selected visual backdrop while launching the window on an opaque surface
// with reduced motion.
constexpr qint64 performanceModeSchemaVersion = 24;
constexpr qint64 terminalInteractionSchemaVersion = 25;
constexpr qint64 terminalSelectionSettingsSchemaVersion = 26;
constexpr qint64 terminalSelectionInteractionSchemaVersion = 27;
// Version 28 expands the mainstream default word separators for shell prompt
// identities while preserving every customized separator set.
constexpr qint64 terminalPromptDelimiterSchemaVersion = 28;
constexpr qint64 localShellSchemaVersion = 29;
constexpr qint64 currentSchemaVersion = localShellSchemaVersion;

using ztermy::config::AccentPreference;
using ztermy::config::AiPermissionPreference;
using ztermy::config::AiProviderPreference;
using ztermy::config::AiProxyPreference;
using ztermy::config::AiReasoningPreference;
using ztermy::config::ApplicationSettings;
using ztermy::config::ApplicationSettingsStoreError;
using ztermy::config::BackdropPreference;
using ztermy::config::CredentialStoragePreference;
using ztermy::config::CursorPreference;
using ztermy::config::LanguagePreference;
using ztermy::config::LocalShellPreference;
using ztermy::config::TerminalMiddleClickPreference;
using ztermy::config::TerminalRightClickPreference;
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
    if (token == QStringLiteral("solid"))
    {
        return BackdropPreference::solid;
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
[[nodiscard]] std::optional<TerminalRightClickPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("context-menu"))
    {
        return TerminalRightClickPreference::contextMenu;
    }
    if (token == QStringLiteral("copy-paste"))
    {
        return TerminalRightClickPreference::copyPaste;
    }
    if (token == QStringLiteral("paste"))
    {
        return TerminalRightClickPreference::paste;
    }
    if (token == QStringLiteral("select-word"))
    {
        return TerminalRightClickPreference::selectWord;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<TerminalMiddleClickPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("disabled"))
    {
        return TerminalMiddleClickPreference::disabled;
    }
    if (token == QStringLiteral("paste"))
    {
        return TerminalMiddleClickPreference::paste;
    }
    if (token == QStringLiteral("context-menu"))
    {
        return TerminalMiddleClickPreference::contextMenu;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<LocalShellPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("automatic"))
    {
        return LocalShellPreference::automatic;
    }
    if (token == QStringLiteral("powerShellCore"))
    {
        return LocalShellPreference::powerShellCore;
    }
    if (token == QStringLiteral("windowsPowerShell"))
    {
        return LocalShellPreference::windowsPowerShell;
    }
    if (token == QStringLiteral("commandPrompt"))
    {
        return LocalShellPreference::commandPrompt;
    }
    if (token == QStringLiteral("gitBash"))
    {
        return LocalShellPreference::gitBash;
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

template <>
[[nodiscard]] std::optional<AiProviderPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("openai-responses"))
    {
        return AiProviderPreference::openAiResponses;
    }
    if (token == QStringLiteral("openai-chatgpt"))
    {
        return AiProviderPreference::openAiChatGpt;
    }
    if (token == QStringLiteral("ollama"))
    {
        return AiProviderPreference::ollama;
    }
    if (token == QStringLiteral("openai-compatible"))
    {
        return AiProviderPreference::openAiCompatible;
    }
    if (token == QStringLiteral("anthropic"))
    {
        return AiProviderPreference::anthropic;
    }
    if (token == QStringLiteral("deepseek"))
    {
        return AiProviderPreference::deepSeek;
    }
    if (token == QStringLiteral("kimi"))
    {
        return AiProviderPreference::kimi;
    }
    if (token == QStringLiteral("zai"))
    {
        return AiProviderPreference::zai;
    }
    if (token == QStringLiteral("gemini"))
    {
        return AiProviderPreference::gemini;
    }
    if (token == QStringLiteral("openrouter"))
    {
        return AiProviderPreference::openRouter;
    }
    if (token == QStringLiteral("qwen"))
    {
        return AiProviderPreference::qwen;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<AiPermissionPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("read-only") || token == QStringLiteral("observer"))
    {
        return AiPermissionPreference::readOnly;
    }
    if (token == QStringLiteral("ask") || token == QStringLiteral("ask-each-write"))
    {
        return AiPermissionPreference::ask;
    }
    if (token == QStringLiteral("auto") || token == QStringLiteral("session-auto"))
    {
        return AiPermissionPreference::automatic;
    }
    if (token == QStringLiteral("yolo") || token == QStringLiteral("saved-host-auto"))
    {
        return AiPermissionPreference::yolo;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<AiReasoningPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("auto"))
    {
        return AiReasoningPreference::automatic;
    }
    if (token == QStringLiteral("off"))
    {
        return AiReasoningPreference::disabled;
    }
    if (token == QStringLiteral("low"))
    {
        return AiReasoningPreference::low;
    }
    if (token == QStringLiteral("medium"))
    {
        return AiReasoningPreference::medium;
    }
    if (token == QStringLiteral("high"))
    {
        return AiReasoningPreference::high;
    }
    if (token == QStringLiteral("max"))
    {
        return AiReasoningPreference::maximum;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<AiProxyPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("system"))
    {
        return AiProxyPreference::system;
    }
    if (token == QStringLiteral("direct"))
    {
        return AiProxyPreference::direct;
    }
    if (token == QStringLiteral("custom"))
    {
        return AiProxyPreference::custom;
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
    const bool validShortcuts =
        settings.shortcutOverrides.size() <= 128
        && std::ranges::all_of(settings.shortcutOverrides.asKeyValueRange(), [](const auto &entry) {
               const auto &[id, shortcut] = entry;
               return !id.trimmed().isEmpty() && id.size() <= 128 && shortcut.size() <= 128;
           });
    const QUrl aiBaseUrl(settings.aiBaseUrl.trimmed());
    const bool validAiBaseUrl =
        aiBaseUrl.isValid() && !aiBaseUrl.host().isEmpty() && aiBaseUrl.userInfo().isEmpty()
        && (aiBaseUrl.scheme() == QStringLiteral("http") || aiBaseUrl.scheme() == QStringLiteral("https"));
    const QString credentialReference = settings.aiCredentialReference.trimmed();
    const bool validCredentialReference =
        !credentialReference.isEmpty() && credentialReference.size() <= 128
        && std::ranges::all_of(credentialReference, [](const QChar character) {
               return character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_');
           });
    const QUrl aiProxyUrl(settings.aiProxyUrl.trimmed());
    const bool validAiProxy =
        settings.aiProxy != AiProxyPreference::custom
        || (aiProxyUrl.isValid() && !aiProxyUrl.host().isEmpty() && aiProxyUrl.userInfo().isEmpty()
            && (aiProxyUrl.scheme() == QStringLiteral("http") || aiProxyUrl.scheme() == QStringLiteral("socks5")));
    return settings.backdropOpacity >= 0.0 && settings.backdropOpacity <= 1.0
           && settings.terminalBackgroundOpacity >= 0.0 && settings.terminalBackgroundOpacity <= 1.0
           && uiFontFamily.size() <= 128 && !fontFamily.isEmpty() && fontFamily.size() <= 128
           && settings.terminalFontSize >= 8 && settings.terminalFontSize <= 32 && validCustomAccent && validShortcuts
           && validAiBaseUrl && settings.aiBaseUrl.size() <= 2048 && settings.aiEndpointPath.size() <= 512
           && settings.aiModel.size() <= 256 && validCredentialReference && validAiProxy
           && settings.aiProxyUrl.size() <= 2048 && settings.aiProxyUsername.size() <= 256
           && settings.terminalWordDelimiters.size() <= 128 && settings.terminalScrollRows >= 1
           && settings.terminalScrollRows <= 20;
}

[[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError> parseSettings(const QJsonObject &root)
{
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    const qint64 version = versionValue.toInteger();
    if (version < legacySchemaVersion || version > currentSchemaVersion)
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
    const QJsonValue keepSelectionAfterCopyValue = root.value(QStringLiteral("keepSelectionAfterCopy"));
    const QJsonValue confirmMultilinePasteValue = root.value(QStringLiteral("confirmMultilinePaste"));
    const QJsonValue terminalRightClickValue = root.value(QStringLiteral("terminalRightClick"));
    const QJsonValue terminalMiddleClickValue = root.value(QStringLiteral("terminalMiddleClick"));
    const QJsonValue terminalWordDelimitersValue = root.value(QStringLiteral("terminalWordDelimiters"));
    const QJsonValue terminalScrollRowsValue = root.value(QStringLiteral("terminalScrollRows"));
    const QJsonValue localShellValue = root.value(QStringLiteral("localShell"));
    const QJsonValue sftpShowHiddenFilesValue = root.value(QStringLiteral("sftpShowHiddenFiles"));
    const QJsonValue sftpConfirmDeleteValue = root.value(QStringLiteral("sftpConfirmDelete"));
    const QJsonValue closeToTrayValue = root.value(QStringLiteral("closeToTray"));
    const QJsonValue performanceModeValue = root.value(QStringLiteral("performanceMode"));
    const QJsonValue credentialStorageValue = root.value(QStringLiteral("credentialStorage"));
    const QJsonValue languageValue = root.value(QStringLiteral("language"));
    const QJsonValue shortcutOverridesValue = root.value(QStringLiteral("shortcutOverrides"));
    const QJsonValue aiProviderValue = root.value(QStringLiteral("aiProvider"));
    const QJsonValue aiBaseUrlValue = root.value(QStringLiteral("aiBaseUrl"));
    const QJsonValue aiEndpointPathValue = root.value(QStringLiteral("aiEndpointPath"));
    const QJsonValue aiModelValue = root.value(QStringLiteral("aiModel"));
    const QJsonValue aiCredentialReferenceValue = root.value(QStringLiteral("aiCredentialReference"));
    const QJsonValue aiAutomaticContextValue = root.value(QStringLiteral("aiAutomaticContext"));
    const QJsonValue aiPermissionValue = root.value(QStringLiteral("aiPermission"));
    const QJsonValue aiConversationHistoryEnabledValue = root.value(QStringLiteral("aiConversationHistoryEnabled"));
    const QJsonValue aiDebugTraceEnabledValue = root.value(QStringLiteral("aiDebugTraceEnabled"));
    const QJsonValue aiReasoningValue = root.value(QStringLiteral("aiReasoning"));
    const QJsonValue aiProxyValue = root.value(QStringLiteral("aiProxy"));
    const QJsonValue aiProxyUrlValue = root.value(QStringLiteral("aiProxyUrl"));
    const QJsonValue aiProxyUsernameValue = root.value(QStringLiteral("aiProxyUsername"));

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
    if (version >= fontOptionsSchemaVersion
        && (!uiFontFamilyValue.isString() || !showAllTerminalFontsValue.isBool() || !terminalLigaturesValue.isBool()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= shortcutSchemaVersion && !shortcutOverridesValue.isObject())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= sftpSchemaVersion && (!sftpShowHiddenFilesValue.isBool() || !sftpConfirmDeleteValue.isBool()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= closeToTraySchemaVersion && !closeToTrayValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= performanceModeSchemaVersion && !performanceModeValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= terminalInteractionSchemaVersion && !terminalRightClickValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= terminalSelectionSettingsSchemaVersion
        && (!terminalMiddleClickValue.isString() || !terminalWordDelimitersValue.isString()
            || !terminalScrollRowsValue.isDouble()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= terminalSelectionInteractionSchemaVersion && !keepSelectionAfterCopyValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= localShellSchemaVersion && !localShellValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiProviderSchemaVersion
        && (!aiProviderValue.isString() || !aiBaseUrlValue.isString() || !aiEndpointPathValue.isString()
            || !aiModelValue.isString() || !aiCredentialReferenceValue.isString() || !aiAutomaticContextValue.isBool()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiPermissionSchemaVersion && !aiPermissionValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiConversationHistorySchemaVersion && !aiConversationHistoryEnabledValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiDebugTraceSchemaVersion && !aiDebugTraceEnabledValue.isBool())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiReasoningSchemaVersion && !aiReasoningValue.isString())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    if (version >= aiProxySchemaVersion
        && (!aiProxyValue.isString() || !aiProxyUrlValue.isString() || !aiProxyUsernameValue.isString()))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    const auto theme = parsePreference<ThemePreference>(themeValue.toString());
    const auto backdrop = parsePreference<BackdropPreference>(backdropValue.toString());
    const auto accent = version >= accentSchemaVersion ? parsePreference<AccentPreference>(accentValue.toString())
                                                       : std::optional{AccentPreference::ztermy};
    const auto cursor = parsePreference<CursorPreference>(cursorValue.toString());
    const auto terminalRightClick =
        version >= terminalInteractionSchemaVersion
            ? parsePreference<TerminalRightClickPreference>(terminalRightClickValue.toString())
            : std::optional{TerminalRightClickPreference::contextMenu};
    const auto terminalMiddleClick =
        version >= terminalSelectionSettingsSchemaVersion
            ? parsePreference<TerminalMiddleClickPreference>(terminalMiddleClickValue.toString())
            : std::optional{TerminalMiddleClickPreference::disabled};
    const auto localShell = version >= localShellSchemaVersion
                                ? parsePreference<LocalShellPreference>(localShellValue.toString())
                                : std::optional{LocalShellPreference::automatic};
    const auto credentialStorage = version >= credentialStorageSchemaVersion
                                       ? parsePreference<CredentialStoragePreference>(credentialStorageValue.toString())
                                       : std::optional{CredentialStoragePreference::automatic};
    const auto language = version >= localizationSchemaVersion
                              ? parsePreference<LanguagePreference>(languageValue.toString())
                              : std::optional{LanguagePreference::system};
    const auto aiProvider = version >= aiProviderSchemaVersion
                                ? parsePreference<AiProviderPreference>(aiProviderValue.toString())
                                : std::optional{AiProviderPreference::openAiResponses};
    const auto aiPermission =
        version >= aiPermissionSchemaVersion
            ? (version < aiPermissionContractSchemaVersion && aiPermissionValue.toString() == QStringLiteral("edit")
                   ? std::optional{AiPermissionPreference::ask}
                   : parsePreference<AiPermissionPreference>(aiPermissionValue.toString()))
            : std::optional{AiPermissionPreference::ask};
    const auto aiReasoning = version >= aiReasoningSchemaVersion
                                 ? parsePreference<AiReasoningPreference>(aiReasoningValue.toString())
                                 : std::optional{AiReasoningPreference::automatic};
    const auto aiProxy = version >= aiProxySchemaVersion ? parsePreference<AiProxyPreference>(aiProxyValue.toString())
                                                         : std::optional{AiProxyPreference::system};
    const qint64 fontSize = fontSizeValue.toInteger(-1);
    const qint64 terminalScrollRows =
        version >= terminalSelectionSettingsSchemaVersion ? terminalScrollRowsValue.toInteger(-1) : 3;
    if (!theme || !backdrop || !accent || !cursor || !terminalRightClick || !terminalMiddleClick || !localShell
        || !credentialStorage || !language || !aiProvider || !aiPermission || !aiReasoning || !aiProxy
        || fontSizeValue.toDouble() != static_cast<double>(fontSize)
        || (version >= terminalSelectionSettingsSchemaVersion
            && terminalScrollRowsValue.toDouble() != static_cast<double>(terminalScrollRows)))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }

    QMap<QString, QString> shortcutOverrides;
    if (version >= shortcutSchemaVersion)
    {
        const QJsonObject shortcutObject = shortcutOverridesValue.toObject();
        if (shortcutObject.size() > 128)
        {
            return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
        }
        for (auto entry = shortcutObject.constBegin(); entry != shortcutObject.constEnd(); ++entry)
        {
            if (!entry.value().isString())
            {
                return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
            }
            shortcutOverrides.insert(entry.key(), entry.value().toString());
        }
    }

    ApplicationSettings settings{
        .backdropOpacity = opacityValue.toDouble(),
        .terminalBackgroundOpacity =
            version >= terminalAppearanceSchemaVersion ? terminalBackgroundOpacityValue.toDouble() : 1.0,
        .shortcutOverrides = std::move(shortcutOverrides),
        .customAccent = version >= accentSchemaVersion ? customAccentValue.toString() : QStringLiteral("#22C55E"),
        .uiFontFamily = version >= fontOptionsSchemaVersion ? uiFontFamilyValue.toString() : QString{},
        .terminalFontFamily = fontFamilyValue.toString(),
        .terminalWordDelimiters =
            version < terminalSelectionSettingsSchemaVersion ? QStringLiteral(" \t'\"│`|;,()[]{}<>$@:#~")
            : version < terminalPromptDelimiterSchemaVersion
                    && terminalWordDelimitersValue.toString() == QStringLiteral(" \t'\"│`|;,()[]{}<>$")
                ? QStringLiteral(" \t'\"│`|;,()[]{}<>$@:#~")
                : terminalWordDelimitersValue.toString(),
        .aiBaseUrl = version >= aiProviderSchemaVersion ? aiBaseUrlValue.toString()
                                                        : QStringLiteral("https://api.openai.com/v1"),
        .aiEndpointPath = version >= aiProviderSchemaVersion ? aiEndpointPathValue.toString() : QString{},
        .aiModel = version >= aiProviderSchemaVersion ? aiModelValue.toString() : QString{},
        .aiCredentialReference =
            version >= aiProviderSchemaVersion ? aiCredentialReferenceValue.toString() : QStringLiteral("ai-default"),
        .aiProxyUrl = version >= aiProxySchemaVersion ? aiProxyUrlValue.toString() : QString{},
        .aiProxyUsername = version >= aiProxySchemaVersion ? aiProxyUsernameValue.toString() : QString{},
        .terminalFontSize = static_cast<int>(fontSize),
        .terminalScrollRows = static_cast<int>(terminalScrollRows),
        .localShell = *localShell,
        .theme = *theme,
        .backdrop = *backdrop,
        .accent = *accent,
        .showAllTerminalFonts = version >= fontOptionsSchemaVersion && showAllTerminalFontsValue.toBool(),
        .terminalLigatures = version < fontOptionsSchemaVersion || terminalLigaturesValue.toBool(),
        .cursor = *cursor,
        .cursorBlink = cursorBlinkValue.toBool(),
        .copyOnSelect = copyOnSelectValue.toBool(),
        .keepSelectionAfterCopy =
            version >= terminalSelectionInteractionSchemaVersion && keepSelectionAfterCopyValue.toBool(),
        .confirmMultilinePaste = confirmMultilinePasteValue.toBool(),
        .terminalRightClick = *terminalRightClick,
        .terminalMiddleClick = *terminalMiddleClick,
        .sftpShowHiddenFiles = version >= sftpSchemaVersion && sftpShowHiddenFilesValue.toBool(),
        .sftpConfirmDelete = version < sftpSchemaVersion || sftpConfirmDeleteValue.toBool(),
        .closeToTray = version >= closeToTraySchemaVersion && closeToTrayValue.toBool(),
        .performanceMode = version >= performanceModeSchemaVersion && performanceModeValue.toBool(),
        .credentialStorage = *credentialStorage,
        .language = *language,
        .aiProvider = *aiProvider,
        .aiAutomaticContext = version >= aiExplicitContextSchemaVersion && aiAutomaticContextValue.toBool(),
        .aiPermission = *aiPermission,
        .aiConversationHistoryEnabled =
            version < aiConversationFirstSchemaVersion || aiConversationHistoryEnabledValue.toBool(),
        .aiDebugTraceEnabled = version >= aiDebugTraceSchemaVersion && aiDebugTraceEnabledValue.toBool(),
        .aiReasoning = *aiReasoning,
        .aiProxy = *aiProxy,
    };
    if (!validSettings(settings))
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    settings.terminalFontFamily = settings.terminalFontFamily.trimmed();
    settings.uiFontFamily = settings.uiFontFamily.trimmed();
    settings.customAccent = settings.customAccent.trimmed().toUpper();
    settings.aiBaseUrl = settings.aiBaseUrl.trimmed();
    settings.aiEndpointPath = settings.aiEndpointPath.trimmed();
    settings.aiModel = settings.aiModel.trimmed();
    settings.aiCredentialReference = settings.aiCredentialReference.trimmed();
    settings.aiProxyUrl = settings.aiProxyUrl.trimmed();
    settings.aiProxyUsername = settings.aiProxyUsername.trimmed();
    return settings;
}

[[nodiscard]] std::expected<ApplicationSettings, ApplicationSettingsStoreError>
parseSettingsPayload(const QByteArrayView payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(ApplicationSettingsStoreError::invalidFormat);
    }
    return parseSettings(document.object());
}

[[nodiscard]] ztermy::persistence::PayloadValidation validateSettingsPayload(const QByteArrayView payload)
{
    const auto parsed = parseSettingsPayload(payload);
    if (parsed)
    {
        return ztermy::persistence::PayloadValidation::valid;
    }
    return parsed.error() == ApplicationSettingsStoreError::unsupportedVersion
               ? ztermy::persistence::PayloadValidation::unsupportedVersion
               : ztermy::persistence::PayloadValidation::invalid;
}

[[nodiscard]] ApplicationSettingsStoreError settingsStoreError(const ztermy::persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case ztermy::persistence::LastKnownGoodError::invalidPath:
            return ApplicationSettingsStoreError::invalidPath;
        case ztermy::persistence::LastKnownGoodError::io:
            return ApplicationSettingsStoreError::ioError;
        case ztermy::persistence::LastKnownGoodError::unsupportedVersion:
            return ApplicationSettingsStoreError::unsupportedVersion;
        case ztermy::persistence::LastKnownGoodError::invalidFormat:
        default:
            return ApplicationSettingsStoreError::invalidFormat;
    }
}

} // namespace

namespace ztermy::config
{

ApplicationSettingsStore::ApplicationSettingsStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &ApplicationSettingsStore::filePath() const noexcept
{
    return m_filePath;
}

bool ApplicationSettingsStore::lastLoadRecoveredFromBackup() const noexcept
{
    return m_lastLoadRecoveredFromBackup;
}

std::expected<ApplicationSettings, ApplicationSettingsStoreError> ApplicationSettingsStore::load() const
{
    m_lastLoadRecoveredFromBackup = false;
    auto loaded = ztermy::persistence::loadLastKnownGood(m_filePath, maximumSettingsFileSize, validateSettingsPayload);
    if (!loaded)
    {
        return std::unexpected(settingsStoreError(loaded.error()));
    }
    if (!loaded->has_value())
    {
        return ApplicationSettings{};
    }
    m_lastLoadRecoveredFromBackup = loaded->value().recoveredFromBackup;
    return parseSettingsPayload(loaded->value().bytes);
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

    QJsonObject shortcutOverrides;
    for (auto entry = settings.shortcutOverrides.cbegin(); entry != settings.shortcutOverrides.cend(); ++entry)
    {
        shortcutOverrides.insert(entry.key(), entry.value());
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
        {QStringLiteral("keepSelectionAfterCopy"), settings.keepSelectionAfterCopy},
        {QStringLiteral("confirmMultilinePaste"), settings.confirmMultilinePaste},
        {QStringLiteral("terminalRightClick"), terminalRightClickPreferenceToken(settings.terminalRightClick)},
        {QStringLiteral("terminalMiddleClick"), terminalMiddleClickPreferenceToken(settings.terminalMiddleClick)},
        {QStringLiteral("terminalWordDelimiters"), settings.terminalWordDelimiters},
        {QStringLiteral("terminalScrollRows"), settings.terminalScrollRows},
        {QStringLiteral("localShell"), localShellPreferenceToken(settings.localShell)},
        {QStringLiteral("sftpShowHiddenFiles"), settings.sftpShowHiddenFiles},
        {QStringLiteral("sftpConfirmDelete"), settings.sftpConfirmDelete},
        {QStringLiteral("closeToTray"), settings.closeToTray},
        {QStringLiteral("performanceMode"), settings.performanceMode},
        {QStringLiteral("credentialStorage"), credentialStoragePreferenceToken(settings.credentialStorage)},
        {QStringLiteral("language"), languagePreferenceToken(settings.language)},
        {QStringLiteral("shortcutOverrides"), shortcutOverrides},
        {QStringLiteral("aiProvider"), aiProviderPreferenceToken(settings.aiProvider)},
        {QStringLiteral("aiBaseUrl"), settings.aiBaseUrl.trimmed()},
        {QStringLiteral("aiEndpointPath"), settings.aiEndpointPath.trimmed()},
        {QStringLiteral("aiModel"), settings.aiModel.trimmed()},
        {QStringLiteral("aiCredentialReference"), settings.aiCredentialReference.trimmed()},
        {QStringLiteral("aiAutomaticContext"), settings.aiAutomaticContext},
        {QStringLiteral("aiPermission"), aiPermissionPreferenceToken(settings.aiPermission)},
        {QStringLiteral("aiConversationHistoryEnabled"), settings.aiConversationHistoryEnabled},
        {QStringLiteral("aiDebugTraceEnabled"), settings.aiDebugTraceEnabled},
        {QStringLiteral("aiReasoning"), aiReasoningPreferenceToken(settings.aiReasoning)},
        {QStringLiteral("aiProxy"), aiProxyPreferenceToken(settings.aiProxy)},
        {QStringLiteral("aiProxyUrl"), settings.aiProxyUrl.trimmed()},
        {QStringLiteral("aiProxyUsername"), settings.aiProxyUsername.trimmed()},
    };

    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (const auto saved =
            ztermy::persistence::saveLastKnownGood(m_filePath, data, maximumSettingsFileSize, validateSettingsPayload);
        !saved)
    {
        return std::unexpected(settingsStoreError(saved.error()));
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
        case BackdropPreference::solid:
            return QStringLiteral("solid");
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

QString terminalRightClickPreferenceToken(const TerminalRightClickPreference preference)
{
    switch (preference)
    {
        case TerminalRightClickPreference::copyPaste:
            return QStringLiteral("copy-paste");
        case TerminalRightClickPreference::paste:
            return QStringLiteral("paste");
        case TerminalRightClickPreference::selectWord:
            return QStringLiteral("select-word");
        case TerminalRightClickPreference::contextMenu:
        default:
            return QStringLiteral("context-menu");
    }
}

QString terminalMiddleClickPreferenceToken(const TerminalMiddleClickPreference preference)
{
    switch (preference)
    {
        case TerminalMiddleClickPreference::paste:
            return QStringLiteral("paste");
        case TerminalMiddleClickPreference::contextMenu:
            return QStringLiteral("context-menu");
        case TerminalMiddleClickPreference::disabled:
        default:
            return QStringLiteral("disabled");
    }
}

QString localShellPreferenceToken(const LocalShellPreference preference)
{
    switch (preference)
    {
        case LocalShellPreference::powerShellCore:
            return QStringLiteral("powerShellCore");
        case LocalShellPreference::windowsPowerShell:
            return QStringLiteral("windowsPowerShell");
        case LocalShellPreference::commandPrompt:
            return QStringLiteral("commandPrompt");
        case LocalShellPreference::gitBash:
            return QStringLiteral("gitBash");
        case LocalShellPreference::automatic:
        default:
            return QStringLiteral("automatic");
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

QString aiProviderPreferenceToken(const AiProviderPreference preference)
{
    switch (preference)
    {
        case AiProviderPreference::anthropic:
            return QStringLiteral("anthropic");
        case AiProviderPreference::deepSeek:
            return QStringLiteral("deepseek");
        case AiProviderPreference::kimi:
            return QStringLiteral("kimi");
        case AiProviderPreference::zai:
            return QStringLiteral("zai");
        case AiProviderPreference::gemini:
            return QStringLiteral("gemini");
        case AiProviderPreference::openRouter:
            return QStringLiteral("openrouter");
        case AiProviderPreference::qwen:
            return QStringLiteral("qwen");
        case AiProviderPreference::ollama:
            return QStringLiteral("ollama");
        case AiProviderPreference::openAiCompatible:
            return QStringLiteral("openai-compatible");
        case AiProviderPreference::openAiChatGpt:
            return QStringLiteral("openai-chatgpt");
        case AiProviderPreference::openAiResponses:
        default:
            return QStringLiteral("openai-responses");
    }
}

QString aiPermissionPreferenceToken(const AiPermissionPreference preference)
{
    switch (preference)
    {
        case AiPermissionPreference::readOnly:
            return QStringLiteral("read-only");
        case AiPermissionPreference::automatic:
            return QStringLiteral("auto");
        case AiPermissionPreference::yolo:
            return QStringLiteral("yolo");
        case AiPermissionPreference::ask:
        default:
            return QStringLiteral("ask");
    }
}

QString aiReasoningPreferenceToken(const AiReasoningPreference preference)
{
    switch (preference)
    {
        case AiReasoningPreference::disabled:
            return QStringLiteral("off");
        case AiReasoningPreference::low:
            return QStringLiteral("low");
        case AiReasoningPreference::medium:
            return QStringLiteral("medium");
        case AiReasoningPreference::high:
            return QStringLiteral("high");
        case AiReasoningPreference::maximum:
            return QStringLiteral("max");
        case AiReasoningPreference::automatic:
        default:
            return QStringLiteral("auto");
    }
}

QString aiProxyPreferenceToken(const AiProxyPreference preference)
{
    switch (preference)
    {
        case AiProxyPreference::direct:
            return QStringLiteral("direct");
        case AiProxyPreference::custom:
            return QStringLiteral("custom");
        case AiProxyPreference::system:
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

std::optional<TerminalRightClickPreference> parseTerminalRightClickPreference(const QString &token)
{
    return parsePreference<TerminalRightClickPreference>(token);
}

std::optional<TerminalMiddleClickPreference> parseTerminalMiddleClickPreference(const QString &token)
{
    return parsePreference<TerminalMiddleClickPreference>(token);
}

std::optional<LocalShellPreference> parseLocalShellPreference(const QString &token)
{
    return parsePreference<LocalShellPreference>(token);
}

std::optional<CredentialStoragePreference> parseCredentialStoragePreference(const QString &token)
{
    return parsePreference<CredentialStoragePreference>(token);
}

std::optional<LanguagePreference> parseLanguagePreference(const QString &token)
{
    return parsePreference<LanguagePreference>(token);
}

std::optional<AiProviderPreference> parseAiProviderPreference(const QString &token)
{
    return parsePreference<AiProviderPreference>(token);
}

std::optional<AiPermissionPreference> parseAiPermissionPreference(const QString &token)
{
    return parsePreference<AiPermissionPreference>(token);
}

std::optional<AiReasoningPreference> parseAiReasoningPreference(const QString &token)
{
    return parsePreference<AiReasoningPreference>(token);
}

std::optional<AiProxyPreference> parseAiProxyPreference(const QString &token)
{
    return parsePreference<AiProxyPreference>(token);
}

} // namespace ztermy::config
