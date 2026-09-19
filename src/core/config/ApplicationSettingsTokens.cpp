#include "core/config/ApplicationSettings.h"

#include <optional>

namespace
{

using ztermy::config::AccentPreference;
using ztermy::config::AiPermissionPreference;
using ztermy::config::AiProviderPreference;
using ztermy::config::AiProxyPreference;
using ztermy::config::AiReasoningPreference;
using ztermy::config::BackdropPreference;
using ztermy::config::CredentialStoragePreference;
using ztermy::config::CursorPreference;
using ztermy::config::EffectsTier;
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
    if (token == QStringLiteral("aero"))
    {
        return BackdropPreference::aero;
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
[[nodiscard]] std::optional<EffectsTier> parsePreference(const QString &token)
{
    if (token == QStringLiteral("full"))
    {
        return EffectsTier::full;
    }
    if (token == QStringLiteral("reduced"))
    {
        return EffectsTier::reduced;
    }
    if (token == QStringLiteral("off"))
    {
        return EffectsTier::off;
    }
    return std::nullopt;
}

template <>
[[nodiscard]] std::optional<AccentPreference> parsePreference(const QString &token)
{
    if (token == QStringLiteral("theme"))
        return AccentPreference::theme;
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
    if (token == QStringLiteral("nushell"))
    {
        return LocalShellPreference::nushell;
    }
    if (token == QStringLiteral("wsl"))
    {
        return LocalShellPreference::wsl;
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

} // namespace

namespace ztermy::config
{

QString themePreferenceToken(const ThemePreference preference)
{
    return preference == ThemePreference::system  ? QStringLiteral("system")
           : preference == ThemePreference::light ? QStringLiteral("light")
                                                  : QStringLiteral("dark");
}

QString backdropPreferenceToken(const BackdropPreference preference)
{
    switch (preference)
    {
        case BackdropPreference::acrylic:
            return QStringLiteral("acrylic");
        case BackdropPreference::aero:
            return QStringLiteral("aero");
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

QString effectsTierToken(const EffectsTier tier)
{
    switch (tier)
    {
        case EffectsTier::reduced:
            return QStringLiteral("reduced");
        case EffectsTier::off:
            return QStringLiteral("off");
        case EffectsTier::full:
        default:
            return QStringLiteral("full");
    }
}

QString accentPreferenceToken(const AccentPreference preference)
{
    switch (preference)
    {
        case AccentPreference::theme:
            return QStringLiteral("theme");
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
    return preference == TerminalMiddleClickPreference::paste         ? QStringLiteral("paste")
           : preference == TerminalMiddleClickPreference::contextMenu ? QStringLiteral("context-menu")
                                                                      : QStringLiteral("disabled");
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
        case LocalShellPreference::nushell:
            return QStringLiteral("nushell");
        case LocalShellPreference::wsl:
            return QStringLiteral("wsl");
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

std::optional<EffectsTier> parseEffectsTier(const QString &token)
{
    return parsePreference<EffectsTier>(token);
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
