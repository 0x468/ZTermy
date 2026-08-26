#include "application/AppController.h"

#include "application/ai/AiNativeToolCatalog.h"
#include "application/ai/AiPrivacyDiagnostics.h"
#include "application/ai/AiSystemPromptBuilder.h"
#include "application/ai/AiTerminalFrameTool.h"
#include "application/ai/AiUserSkillTool.h"
#include "application/ai/AiWaitCommandTool.h"
#include "domain/ai/AiCommandEcho.h"
#include "domain/ai/AiContextCompactor.h"
#include "domain/ai/AiContextSerializer.h"
#include "domain/ai/AiProviderRecoveryPolicy.h"
#include "domain/ssh/SshTarget.h"
#include "domain/terminal/ShellPathQuoter.h"
#include "infrastructure/ai/AiTraceSanitizer.h"
#include "infrastructure/ai/ProviderEndpointResolver.h"
#include "infrastructure/ai/ProviderErrorParser.h"
#include "infrastructure/security/InMemoryCredentialVault.h"
#include "infrastructure/workbench/QuickCommandStore.h"
#include "platform/windows/WindowsProtectedClipboard.h"
#include "ui/terminal/TerminalItem.h"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMimeData>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

Q_LOGGING_CATEGORY(appControllerLog, "ztermy.application.controller")

namespace
{

constexpr auto AiProxyCredentialReference = "ai-proxy";

class AiSystemProxyFactory final : public QNetworkProxyFactory
{
public:
    [[nodiscard]] QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
    {
        return systemProxyForQuery(query);
    }
};

[[nodiscard]] QVariantMap
subscriptionUsageWindowValue(const std::optional<ztermy::ai::OpenAiSubscriptionUsageWindow> &window)
{
    if (!window.has_value())
    {
        return {};
    }
    return {{QStringLiteral("usedPercent"), window->usedPercent},
            {QStringLiteral("remainingPercent"), std::clamp(100.0 - window->usedPercent, 0.0, 100.0)},
            {QStringLiteral("durationSeconds"), window->durationSeconds},
            {QStringLiteral("resetAtUtcSeconds"), window->resetAtUtcSeconds}};
}

[[nodiscard]] QVariantMap subscriptionUsageLimitValue(const ztermy::ai::OpenAiSubscriptionUsageLimit &limit)
{
    return {{QStringLiteral("name"), limit.name},
            {QStringLiteral("meteredFeature"), limit.meteredFeature},
            {QStringLiteral("allowed"), limit.allowed},
            {QStringLiteral("reached"), limit.reached},
            {QStringLiteral("primary"), subscriptionUsageWindowValue(limit.primary)},
            {QStringLiteral("secondary"), subscriptionUsageWindowValue(limit.secondary)}};
}

constexpr std::size_t maximumRecentHostProfiles = 6;
constexpr quint64 aiSftpListRequestFlag = quint64{1} << 63U;
constexpr qsizetype maximumAiTextAttachmentBytes = qsizetype{256} * 1024;
constexpr qsizetype maximumAiTextAttachmentFiles = 4;
constexpr qsizetype maximumAiImageAttachmentBytes = qsizetype{5} * 1024 * 1024;
constexpr qsizetype maximumAiImageAttachmentTotalBytes = qsizetype{12} * 1024 * 1024;
constexpr qsizetype maximumAiImageAttachmentFiles = 4;
constexpr quint64 maximumAiImagePixels = quint64{40} * 1024 * 1024;

struct AiTextAttachmentLoadResult final
{
    std::vector<ztermy::ai::AiExplicitContext> attachments;
    QStringList rejectedFiles;
};

struct AiImageAttachmentLoadResult final
{
    std::vector<ztermy::ai::AiImageAttachment> attachments;
    QStringList rejectedFiles;
};

class TerminalOutputFanout final : public ztermy::terminal::TerminalOutputSink
{
public:
    using Observer = std::function<void(std::span<const std::byte>)>;

    TerminalOutputFanout(std::shared_ptr<ztermy::terminal::TerminalOutputSink> primary, Observer observer)
        : m_primary(std::move(primary)), m_observer(std::move(observer))
    {
    }

    void append(const std::span<const std::byte> bytes) noexcept override
    {
        try
        {
            if (m_primary)
            {
                m_primary->append(bytes);
            }
            if (m_observer)
            {
                m_observer(bytes);
            }
        }
        catch (...)
        {
            qCCritical(appControllerLog) << "Terminal output fanout suppressed an exception";
        }
    }

private:
    std::shared_ptr<ztermy::terminal::TerminalOutputSink> m_primary;
    Observer m_observer;
};

[[nodiscard]] ztermy::workbench::ScriptRecorder::TimePoint recorderNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

[[nodiscard]] ztermy::workbench::ScriptExecution::TimePoint scriptExecutionNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

[[nodiscard]] QString scriptRecorderStateToken(const ztermy::workbench::ScriptRecorderState state)
{
    using ztermy::workbench::ScriptRecorderState;
    switch (state)
    {
        case ScriptRecorderState::Idle:
            return QStringLiteral("idle");
        case ScriptRecorderState::Recording:
            return QStringLiteral("recording");
        case ScriptRecorderState::Paused:
            return QStringLiteral("paused");
        case ScriptRecorderState::Review:
            return QStringLiteral("review");
    }
    return QStringLiteral("idle");
}

[[nodiscard]] QString applicationDataFile(const QString &fileName)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName);
}

[[nodiscard]] QString siblingKnownHostsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("known_hosts.json"));
}

[[nodiscard]] QString siblingSettingsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("settings.json"));
}

[[nodiscard]] QString siblingCredentialsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("credentials.zvlt"));
}

[[nodiscard]] QString siblingPortForwardingFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("port_forwarding.json"));
}

[[nodiscard]] QString siblingQuickCommandsFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("quick_commands.json"));
}

[[nodiscard]] QString siblingAiQuickMessagesFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("ai_quick_messages.json"));
}

[[nodiscard]] QString siblingAiUserSkillsDirectory(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("Skills"));
}

[[nodiscard]] QString siblingScriptsFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("scripts.json"));
}

[[nodiscard]] QString siblingNotesDirectory(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("notes"));
}

[[nodiscard]] QString siblingWorkspaceStateFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("workspace_state.json"));
}

[[nodiscard]] QString siblingAiAuditFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("ai_activity.json"));
}

[[nodiscard]] QString siblingAiPermissionRulesFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("ai_permission_rules.json"));
}

[[nodiscard]] QString siblingMcpServersFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("mcp_servers.json"));
}

[[nodiscard]] QString siblingAiConversationHistoryFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("ai_conversations.enc"));
}

[[nodiscard]] QString siblingTransferRecoveryFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("transfer_recovery.json"));
}

[[nodiscard]] QString siblingTransferBatchRecoveryFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("transfer_batch_recovery.json"));
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QVariantMap aiCompactionValue(const ztermy::ai::AiCompactionResult &result,
                                            const QVariantMap &previous = {})
{
    const qulonglong previousItems = previous.value(QStringLiteral("itemCount")).toULongLong();
    const qulonglong previousBytes = previous.value(QStringLiteral("removedBytes")).toULongLong();
    const qulonglong itemCount = previousItems + static_cast<qulonglong>(result.compactedItemCount);
    const qulonglong removedBytes = previousBytes + static_cast<qulonglong>(result.removedBytes);
    const bool compacted = previous.value(QStringLiteral("compacted")).toBool() || result.compacted;
    return {{QStringLiteral("visible"), compacted || result.overBudget},
            {QStringLiteral("compacted"), compacted},
            {QStringLiteral("itemCount"), QVariant::fromValue(itemCount)},
            {QStringLiteral("removedBytes"), QVariant::fromValue(removedBytes)},
            {QStringLiteral("estimatedInputTokens"),
             QVariant::fromValue(static_cast<qulonglong>(result.estimatedInputTokens))},
            {QStringLiteral("overBudget"), result.overBudget}};
}

[[nodiscard]] std::optional<std::string> imageMediaType(QByteArray format)
{
    format = format.toLower();
    if (format == QByteArrayLiteral("jpg") || format == QByteArrayLiteral("jpeg"))
    {
        return std::string{"image/jpeg"};
    }
    if (format == QByteArrayLiteral("png"))
    {
        return std::string{"image/png"};
    }
    if (format == QByteArrayLiteral("webp"))
    {
        return std::string{"image/webp"};
    }
    if (format == QByteArrayLiteral("gif"))
    {
        return std::string{"image/gif"};
    }
    return std::nullopt;
}

[[nodiscard]] bool isAiImageFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
           || suffix == QStringLiteral("webp") || suffix == QStringLiteral("gif");
}

[[nodiscard]] std::optional<ztermy::ai::AiImageAttachment> aiImageAttachmentFromBytes(const QString &fileName,
                                                                                      QByteArray bytes)
{
    if (bytes.isEmpty() || bytes.size() > maximumAiImageAttachmentBytes)
    {
        return std::nullopt;
    }
    QBuffer sourceBuffer(&bytes);
    if (!sourceBuffer.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    QImageReader reader(&sourceBuffer);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead())
    {
        return std::nullopt;
    }
    const auto mediaType = imageMediaType(reader.format());
    const QSize imageSize = reader.size();
    if (!mediaType.has_value() || !imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0
        || static_cast<quint64>(imageSize.width()) * static_cast<quint64>(imageSize.height()) > maximumAiImagePixels)
    {
        return std::nullopt;
    }

    reader.setAutoTransform(true);
    reader.setScaledSize(imageSize.scaled(QSize(512, 384), Qt::KeepAspectRatio));
    const QImage preview = reader.read();
    QByteArray previewBytes;
    QBuffer previewBuffer(&previewBytes);
    if (preview.isNull() || !previewBuffer.open(QIODevice::WriteOnly) || !preview.save(&previewBuffer, "PNG"))
    {
        return std::nullopt;
    }

    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    return ztermy::ai::AiImageAttachment{
        .id = "image:" + digest.toStdString(),
        .fileName = utf8String(fileName),
        .mediaType = *mediaType,
        .base64Data = bytes.toBase64().toStdString(),
        .previewBase64Data = previewBytes.toBase64().toStdString(),
        .byteSize = static_cast<std::uint64_t>(bytes.size()),
        .pixelWidth = static_cast<std::uint32_t>(imageSize.width()),
        .pixelHeight = static_cast<std::uint32_t>(imageSize.height()),
    };
}

[[nodiscard]] ztermy::ai::AiProviderKind aiProviderKind(const ztermy::config::AiProviderPreference provider) noexcept
{
    switch (provider)
    {
        case ztermy::config::AiProviderPreference::openAiResponses:
        case ztermy::config::AiProviderPreference::openAiChatGpt:
            return ztermy::ai::AiProviderKind::openAiResponses;
        case ztermy::config::AiProviderPreference::anthropic:
            return ztermy::ai::AiProviderKind::anthropicMessages;
        case ztermy::config::AiProviderPreference::deepSeek:
        case ztermy::config::AiProviderPreference::kimi:
        case ztermy::config::AiProviderPreference::zai:
        case ztermy::config::AiProviderPreference::gemini:
        case ztermy::config::AiProviderPreference::openRouter:
        case ztermy::config::AiProviderPreference::qwen:
        case ztermy::config::AiProviderPreference::openAiCompatible:
            return ztermy::ai::AiProviderKind::openAiCompatible;
        case ztermy::config::AiProviderPreference::ollama:
            return ztermy::ai::AiProviderKind::ollama;
    }
    return ztermy::ai::AiProviderKind::openAiResponses;
}

[[nodiscard]] ztermy::ai::AiProviderFlavor
aiProviderFlavor(const ztermy::config::AiProviderPreference provider) noexcept
{
    using Flavor = ztermy::ai::AiProviderFlavor;
    switch (provider)
    {
        case ztermy::config::AiProviderPreference::openAiResponses:
        case ztermy::config::AiProviderPreference::openAiChatGpt:
            return Flavor::openAi;
        case ztermy::config::AiProviderPreference::anthropic:
            return Flavor::anthropic;
        case ztermy::config::AiProviderPreference::deepSeek:
            return Flavor::deepSeek;
        case ztermy::config::AiProviderPreference::kimi:
            return Flavor::kimi;
        case ztermy::config::AiProviderPreference::zai:
            return Flavor::zai;
        case ztermy::config::AiProviderPreference::gemini:
            return Flavor::gemini;
        case ztermy::config::AiProviderPreference::openRouter:
            return Flavor::openRouter;
        case ztermy::config::AiProviderPreference::qwen:
            return Flavor::qwen;
        case ztermy::config::AiProviderPreference::ollama:
            return Flavor::ollama;
        case ztermy::config::AiProviderPreference::openAiCompatible:
            return Flavor::compatible;
    }
    return Flavor::openAi;
}

[[nodiscard]] ztermy::ai::AiReasoningEffort
aiReasoningEffort(const ztermy::config::AiReasoningPreference preference) noexcept
{
    using Effort = ztermy::ai::AiReasoningEffort;
    switch (preference)
    {
        case ztermy::config::AiReasoningPreference::automatic:
            return Effort::automatic;
        case ztermy::config::AiReasoningPreference::disabled:
            return Effort::disabled;
        case ztermy::config::AiReasoningPreference::low:
            return Effort::low;
        case ztermy::config::AiReasoningPreference::medium:
            return Effort::medium;
        case ztermy::config::AiReasoningPreference::high:
            return Effort::high;
        case ztermy::config::AiReasoningPreference::maximum:
            return Effort::maximum;
    }
    return Effort::automatic;
}

[[nodiscard]] QStringList supportedAiReasoningTokens(const ztermy::config::AiProviderPreference provider,
                                                     const QString &model)
{
    switch (provider)
    {
        case ztermy::config::AiProviderPreference::openAiResponses:
        case ztermy::config::AiProviderPreference::openAiChatGpt:
        case ztermy::config::AiProviderPreference::anthropic:
            return {QStringLiteral("auto"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                    QStringLiteral("max")};
        case ztermy::config::AiProviderPreference::deepSeek:
            return {QStringLiteral("auto"), QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("high"),
                    QStringLiteral("max")};
        case ztermy::config::AiProviderPreference::kimi:
        {
            const QString normalizedModel = model.trimmed().toLower();
            if (normalizedModel.startsWith(QStringLiteral("kimi-k3")))
            {
                return {QStringLiteral("auto"), QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
            }
            if (normalizedModel.startsWith(QStringLiteral("kimi-k2.5"))
                || normalizedModel.startsWith(QStringLiteral("kimi-k2.6")))
            {
                return {QStringLiteral("auto"), QStringLiteral("off")};
            }
            return {QStringLiteral("auto")};
        }
        case ztermy::config::AiProviderPreference::zai:
            return {QStringLiteral("auto"), QStringLiteral("off")};
        case ztermy::config::AiProviderPreference::gemini:
            return {QStringLiteral("auto"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};
        case ztermy::config::AiProviderPreference::openRouter:
            return {QStringLiteral("auto"),   QStringLiteral("off"),  QStringLiteral("low"),
                    QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")};
        case ztermy::config::AiProviderPreference::qwen:
            return {QStringLiteral("auto"), QStringLiteral("off")};
        case ztermy::config::AiProviderPreference::ollama:
        case ztermy::config::AiProviderPreference::openAiCompatible:
            return {QStringLiteral("auto")};
    }
    return {QStringLiteral("auto")};
}

[[nodiscard]] QString aiCredentialReference(const ztermy::config::AiProviderPreference provider)
{
    return QStringLiteral("ai-") + ztermy::config::aiProviderPreferenceToken(provider);
}

[[nodiscard]] ztermy::security::SensitiveByteArray
cloneSensitiveSecret(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view value = secret.view();
    return ztermy::security::SensitiveByteArray(QByteArray(value.data(), static_cast<qsizetype>(value.size())));
}

[[nodiscard]] ztermy::ai::AiPermissionMode
aiPermissionMode(const ztermy::config::AiPermissionPreference preference) noexcept
{
    switch (preference)
    {
        case ztermy::config::AiPermissionPreference::readOnly:
            return ztermy::ai::AiPermissionMode::readOnly;
        case ztermy::config::AiPermissionPreference::automatic:
            return ztermy::ai::AiPermissionMode::automatic;
        case ztermy::config::AiPermissionPreference::yolo:
            return ztermy::ai::AiPermissionMode::yolo;
        case ztermy::config::AiPermissionPreference::ask:
            return ztermy::ai::AiPermissionMode::ask;
    }
    return ztermy::ai::AiPermissionMode::ask;
}

[[nodiscard]] QString aiPermissionCapabilityToken(const ztermy::ai::AiPermissionCapability capability)
{
    using ztermy::ai::AiPermissionCapability;
    switch (capability)
    {
        case AiPermissionCapability::terminalCommand:
            return QStringLiteral("terminal-command");
        case AiPermissionCapability::ptyInput:
            return QStringLiteral("pty-input");
        case AiPermissionCapability::terminalInterrupt:
            return QStringLiteral("terminal-interrupt");
        case AiPermissionCapability::runbookMutation:
            return QStringLiteral("runbook");
        case AiPermissionCapability::sftpDownload:
            return QStringLiteral("sftp-download");
        case AiPermissionCapability::sftpUpload:
            return QStringLiteral("sftp-upload");
        case AiPermissionCapability::mcpTool:
            return QStringLiteral("mcp-tool");
    }
    return QStringLiteral("terminal-command");
}

[[nodiscard]] QString aiPermissionMatcherToken(const ztermy::ai::AiPermissionRuleMatcher matcher)
{
    using ztermy::ai::AiPermissionRuleMatcher;
    switch (matcher)
    {
        case AiPermissionRuleMatcher::exact:
            return QStringLiteral("exact");
        case AiPermissionRuleMatcher::prefix:
            return QStringLiteral("prefix");
        case AiPermissionRuleMatcher::glob:
            return QStringLiteral("glob");
        case AiPermissionRuleMatcher::regex:
            return QStringLiteral("regex");
        case AiPermissionRuleMatcher::all:
            return QStringLiteral("all");
    }
    return QStringLiteral("exact");
}

[[nodiscard]] QString aiPermissionDurationToken(const ztermy::ai::AiPermissionRuleDuration duration)
{
    using ztermy::ai::AiPermissionRuleDuration;
    switch (duration)
    {
        case AiPermissionRuleDuration::once:
            return QStringLiteral("once");
        case AiPermissionRuleDuration::session:
            return QStringLiteral("session");
        case AiPermissionRuleDuration::profile:
            return QStringLiteral("profile");
        case AiPermissionRuleDuration::global:
            return QStringLiteral("global");
    }
    return QStringLiteral("session");
}

[[nodiscard]] QString aiPermissionDispositionToken(const ztermy::ai::AiPermissionDisposition disposition)
{
    using ztermy::ai::AiPermissionDisposition;
    switch (disposition)
    {
        case AiPermissionDisposition::allow:
            return QStringLiteral("allow");
        case AiPermissionDisposition::ask:
            return QStringLiteral("ask");
        case AiPermissionDisposition::deny:
            return QStringLiteral("deny");
    }
    return QStringLiteral("ask");
}

[[nodiscard]] std::optional<ztermy::ai::AiPermissionRuleMatcher> parseAiPermissionMatcher(const QString &token)
{
    using ztermy::ai::AiPermissionRuleMatcher;
    if (token == QStringLiteral("exact"))
    {
        return AiPermissionRuleMatcher::exact;
    }
    if (token == QStringLiteral("prefix"))
    {
        return AiPermissionRuleMatcher::prefix;
    }
    if (token == QStringLiteral("glob"))
    {
        return AiPermissionRuleMatcher::glob;
    }
    if (token == QStringLiteral("regex"))
    {
        return AiPermissionRuleMatcher::regex;
    }
    if (token == QStringLiteral("all"))
    {
        return AiPermissionRuleMatcher::all;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ztermy::ai::AiPermissionRuleDuration> parseAiPermissionDuration(const QString &token)
{
    using ztermy::ai::AiPermissionRuleDuration;
    if (token == QStringLiteral("once"))
    {
        return AiPermissionRuleDuration::once;
    }
    if (token == QStringLiteral("session"))
    {
        return AiPermissionRuleDuration::session;
    }
    if (token == QStringLiteral("profile"))
    {
        return AiPermissionRuleDuration::profile;
    }
    if (token == QStringLiteral("global"))
    {
        return AiPermissionRuleDuration::global;
    }
    return std::nullopt;
}

[[nodiscard]] QString semanticCapabilityToken(const ztermy::terminal::TerminalSemanticCapability capability)
{
    switch (capability)
    {
        case ztermy::terminal::TerminalSemanticCapability::none:
            return QStringLiteral("none");
        case ztermy::terminal::TerminalSemanticCapability::basic:
            return QStringLiteral("basic");
        case ztermy::terminal::TerminalSemanticCapability::rich:
            return QStringLiteral("rich");
    }
    return QStringLiteral("none");
}

[[nodiscard]] std::string aiContextAttachmentKind(const ztermy::ai::AiContextItem &item)
{
    if (item.kind == ztermy::ai::AiContextItemKind::commandBlock)
    {
        return "command";
    }
    if (item.kind == ztermy::ai::AiContextItemKind::currentTerminalFrame)
    {
        return "terminal_frame";
    }
    if (item.id.starts_with("terminal-selection"))
    {
        return "selection";
    }
    if (item.id.starts_with("terminal-command"))
    {
        return "command";
    }
    if (item.id.starts_with("local-file"))
    {
        return "file";
    }
    return "attachment";
}

[[nodiscard]] std::vector<ztermy::ai::AiContextAttachmentSummary>
aiContextAttachmentSummaries(const ztermy::ai::AiContextBundle &context)
{
    std::vector<ztermy::ai::AiContextAttachmentSummary> summaries;
    summaries.reserve(context.items.size());
    for (const auto &item : context.items)
    {
        const std::string_view rawTitle = item.title.empty() ? std::string_view(item.id) : std::string_view(item.title);
        const QString title = QString::fromUtf8(rawTitle.data(), static_cast<qsizetype>(rawTitle.size())).left(256);
        const std::string boundedTitle = utf8String(title);
        summaries.push_back({.title = boundedTitle.empty() ? std::string("Attached context") : boundedTitle,
                             .kind = aiContextAttachmentKind(item),
                             .quality = utf8String(semanticCapabilityToken(item.capability)),
                             .redacted = item.redacted,
                             .truncated = item.truncated});
    }
    return summaries;
}

[[nodiscard]] ztermy::terminal::TerminalSemanticCapability
semanticCapability(const ztermy::terminal::SemanticTerminalSnapshot &snapshot) noexcept
{
    if (!snapshot.commandBlocks.empty())
    {
        return snapshot.commandBlocks.back().capability;
    }
    return snapshot.richCapabilityClaimed ? ztermy::terminal::TerminalSemanticCapability::rich
                                          : ztermy::terminal::TerminalSemanticCapability::none;
}

[[nodiscard]] QString terminalFrameText(const ztermy::terminal::TerminalSnapshotPtr &snapshot)
{
    if (!snapshot || snapshot->columns == 0 || snapshot->rows == 0)
    {
        return {};
    }
    QString frame;
    for (std::uint16_t row = 0; row < snapshot->rows; ++row)
    {
        QString line;
        for (std::uint16_t column = 0; column < snapshot->columns; ++column)
        {
            const auto &cell = snapshot->cell(column, row);
            if (cell.displayWidth == 0 || cell.grapheme.empty() || cell.invisible)
            {
                continue;
            }
            line += QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
        }
        while (line.endsWith(u' '))
        {
            line.chop(1);
        }
        frame += line;
        if (row + 1 < snapshot->rows)
        {
            frame += u'\n';
        }
    }
    return frame;
}

[[nodiscard]] ztermy::ai::AiTerminalFrameInput terminalFrameInput(const ztermy::terminal::TerminalSnapshotPtr &snapshot)
{
    if (!snapshot)
    {
        return {};
    }
    const QStringList textLines = terminalFrameText(snapshot).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(textLines.size()));
    for (const QString &line : textLines)
    {
        lines.push_back(utf8String(line));
    }
    return ztermy::ai::AiTerminalFrameInput{.lines = std::move(lines),
                                            .columns = snapshot->columns,
                                            .rows = snapshot->rows,
                                            .cursorColumn = snapshot->cursor.column,
                                            .cursorRow = snapshot->cursor.row,
                                            .cursorVisible = snapshot->cursor.visible};
}

[[nodiscard]] QString authenticationToken(const ztermy::ssh::SshAuthenticationMethod authentication)
{
    switch (authentication)
    {
        case ztermy::ssh::SshAuthenticationMethod::PrivateKey:
            return QStringLiteral("private-key");
        case ztermy::ssh::SshAuthenticationMethod::Password:
            return QStringLiteral("password");
        case ztermy::ssh::SshAuthenticationMethod::Agent:
            return QStringLiteral("agent");
    }
    return {};
}

[[nodiscard]] QString forwardingTypeToken(const ztermy::forwarding::PortForwardingType type)
{
    switch (type)
    {
        case ztermy::forwarding::PortForwardingType::Local:
            return QStringLiteral("local");
        case ztermy::forwarding::PortForwardingType::Remote:
            return QStringLiteral("remote");
        case ztermy::forwarding::PortForwardingType::Dynamic:
            return QStringLiteral("dynamic");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::forwarding::PortForwardingType> parseForwardingType(const QString &value)
{
    if (value == QStringLiteral("local"))
    {
        return ztermy::forwarding::PortForwardingType::Local;
    }
    if (value == QStringLiteral("remote"))
    {
        return ztermy::forwarding::PortForwardingType::Remote;
    }
    if (value == QStringLiteral("dynamic"))
    {
        return ztermy::forwarding::PortForwardingType::Dynamic;
    }
    return std::nullopt;
}

[[nodiscard]] QString forwardingStateToken(const ztermy::forwarding::PortForwardingJobState state)
{
    switch (state)
    {
        case ztermy::forwarding::PortForwardingJobState::Stopped:
            return QStringLiteral("stopped");
        case ztermy::forwarding::PortForwardingJobState::Starting:
            return QStringLiteral("starting");
        case ztermy::forwarding::PortForwardingJobState::Running:
            return QStringLiteral("running");
        case ztermy::forwarding::PortForwardingJobState::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("stopped");
}

[[nodiscard]] QString forwardingFailureToken(const ztermy::forwarding::PortForwardingJobFailure failure)
{
    using ztermy::forwarding::PortForwardingJobFailure;
    switch (failure)
    {
        case PortForwardingJobFailure::None:
            return {};
        case PortForwardingJobFailure::Connection:
            return QStringLiteral("connection");
        case PortForwardingJobFailure::Listener:
            return QStringLiteral("listener");
        case PortForwardingJobFailure::RemoteListener:
            return QStringLiteral("remote-listener");
        case PortForwardingJobFailure::Transport:
            return QStringLiteral("transport");
        case PortForwardingJobFailure::ResourceLimit:
            return QStringLiteral("resource-limit");
    }
    return QStringLiteral("transport");
}

[[nodiscard]] std::optional<ztermy::ssh::SshAuthenticationMethod> parseAuthenticationToken(const QString &value)
{
    if (value == QStringLiteral("private-key"))
    {
        return ztermy::ssh::SshAuthenticationMethod::PrivateKey;
    }
    if (value == QStringLiteral("password"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Password;
    }
    if (value == QStringLiteral("agent"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Agent;
    }
    return std::nullopt;
}

[[nodiscard]] QString proxyTypeToken(const ztermy::ssh::SshProxyType type)
{
    switch (type)
    {
        case ztermy::ssh::SshProxyType::None:
            return QStringLiteral("none");
        case ztermy::ssh::SshProxyType::Socks5:
            return QStringLiteral("socks5");
        case ztermy::ssh::SshProxyType::HttpConnect:
            return QStringLiteral("http-connect");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyType> parseProxyTypeToken(const QString &value)
{
    if (value == QStringLiteral("none"))
    {
        return ztermy::ssh::SshProxyType::None;
    }
    if (value == QStringLiteral("socks5"))
    {
        return ztermy::ssh::SshProxyType::Socks5;
    }
    if (value == QStringLiteral("http-connect"))
    {
        return ztermy::ssh::SshProxyType::HttpConnect;
    }
    return std::nullopt;
}

[[nodiscard]] QString utf8QString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string compactJson(const QJsonObject &value)
{
    const QByteArray encoded = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

[[nodiscard]] std::string aiToolFailureJson(const QString &code, const QString &message)
{
    return compactJson(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

[[nodiscard]] std::string aiRunCommandFrameResult(const QJsonObject &accepted, const std::string &frameJson,
                                                  const bool timedOut)
{
    QJsonParseError parseError;
    const QJsonDocument frameDocument =
        QJsonDocument::fromJson(QByteArray(frameJson.data(), static_cast<qsizetype>(frameJson.size())), &parseError);
    QJsonObject result = accepted;
    result.remove(QStringLiteral("recommended_wait_tool"));
    result.remove(QStringLiteral("frame_wait_strategy"));
    result.remove(QStringLiteral("recommended_idle_ms"));
    result.insert(QStringLiteral("ok"), !timedOut);
    result.insert(QStringLiteral("tool_ok"), true);
    result.insert(QStringLiteral("status"), timedOut ? QStringLiteral("timeout") : QStringLiteral("idle_unverified"));
    result.insert(QStringLiteral("completion_confirmed"), false);
    result.insert(QStringLiteral("command_started"), true);
    result.insert(QStringLiteral("command_completed"), false);
    result.insert(QStringLiteral("command_succeeded"), QJsonValue::Null);
    result.insert(QStringLiteral("lifecycle_quality"), QStringLiteral("frame_idle_unverified"));
    if (parseError.error == QJsonParseError::NoError && frameDocument.isObject())
    {
        result.insert(QStringLiteral("frame"), frameDocument.object().value(QStringLiteral("frame")));
    }
    if (timedOut)
    {
        result.insert(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")},
                        {QStringLiteral("message"),
                         QCoreApplication::translate(
                             "AppController", "The command wait timed out; the returned frame may be partial.")}});
    }
    return compactJson(result);
}

[[nodiscard]] QString aiActivityResultCode(const std::string &outputJson)
{
    QJsonParseError error;
    const auto document =
        QJsonDocument::fromJson(QByteArray(outputJson.data(), static_cast<qsizetype>(outputJson.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return QStringLiteral("invalid_result");
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("ok")).toBool())
    {
        return QStringLiteral("ok");
    }
    QString code = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
    if (!code.isEmpty() && code.size() <= 48 && std::ranges::all_of(code, [](const QChar character) {
            return character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-');
        }))
    {
        return code;
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString aiToolDetailText(const std::string &json)
{
    if (json.empty())
    {
        return {};
    }
    QJsonParseError error;
    const QByteArray bytes(json.data(), static_cast<qsizetype>(json.size()));
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error == QJsonParseError::NoError && (document.isObject() || document.isArray()))
    {
        return QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
    }
    return QString::fromUtf8(bytes);
}

[[nodiscard]] std::string aiBudgetFailureJson(const ztermy::ai::AiAgentBudgetDecision decision)
{
    QString code = QStringLiteral("tool_call_limit");
    switch (decision)
    {
        case ztermy::ai::AiAgentBudgetDecision::allow:
            return {};
        case ztermy::ai::AiAgentBudgetDecision::toolCallLimit:
            break;
        case ztermy::ai::AiAgentBudgetDecision::writeActionLimit:
            code = QStringLiteral("write_action_limit");
            break;
        case ztermy::ai::AiAgentBudgetDecision::repeatedReadLimit:
            code = QStringLiteral("repeated_read_limit");
            break;
        case ztermy::ai::AiAgentBudgetDecision::tokenLimit:
            code = QStringLiteral("token_limit");
            break;
        case ztermy::ai::AiAgentBudgetDecision::timeLimit:
            code = QStringLiteral("time_limit");
            break;
    }
    return aiToolFailureJson(code, QCoreApplication::translate("AiTools", "The AI turn tool budget was exhausted."));
}

[[nodiscard]] std::string aiCommandId()
{
    return utf8String(QStringLiteral("cmd_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
}

[[nodiscard]] QString normalizedTerminalWorkingDirectory(const std::string &value)
{
    if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos)
    {
        return {};
    }
    const QByteArray encoded(value.data(), static_cast<qsizetype>(value.size()));
    QString path;
    if (encoded.startsWith("file://"))
    {
        const QUrl url = QUrl::fromEncoded(encoded, QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != QStringLiteral("file"))
        {
            return {};
        }
        path = url.path(QUrl::FullyDecoded);
    }
    else
    {
        path = QString::fromUtf8(encoded);
    }
    const auto normalized = ztermy::sftp::normalizeRemotePath(utf8String(path));
    return normalized ? utf8QString(*normalized) : QString{};
}

[[nodiscard]] std::optional<ztermy::workbench::ShellScope> quickCommandShellScope(const QString &value)
{
    if (value == QStringLiteral("any"))
    {
        return ztermy::workbench::ShellScope::any;
    }
    if (value == QStringLiteral("posix"))
    {
        return ztermy::workbench::ShellScope::posix;
    }
    if (value == QStringLiteral("powershell"))
    {
        return ztermy::workbench::ShellScope::powershell;
    }
    return std::nullopt;
}

[[nodiscard]] QString quickCommandShellScopeToken(const ztermy::workbench::ShellScope value)
{
    switch (value)
    {
        case ztermy::workbench::ShellScope::posix:
            return QStringLiteral("posix");
        case ztermy::workbench::ShellScope::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellScope::any:
        default:
            return QStringLiteral("any");
    }
}

[[nodiscard]] QString scriptVariableTypeToken(const ztermy::workbench::ScriptVariableType type)
{
    switch (type)
    {
        case ztermy::workbench::ScriptVariableType::integer:
            return QStringLiteral("integer");
        case ztermy::workbench::ScriptVariableType::boolean:
            return QStringLiteral("boolean");
        case ztermy::workbench::ScriptVariableType::choice:
            return QStringLiteral("choice");
        case ztermy::workbench::ScriptVariableType::text:
        default:
            return QStringLiteral("text");
    }
}

[[nodiscard]] QString scriptContinuationToken(const ztermy::workbench::ScriptContinuation continuation)
{
    return continuation == ztermy::workbench::ScriptContinuation::literalOutput ? QStringLiteral("literal-output")
                                                                                : QStringLiteral("immediate");
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptVariableType> parseScriptVariableType(const QString &value)
{
    using ztermy::workbench::ScriptVariableType;
    if (value == QStringLiteral("text"))
    {
        return ScriptVariableType::text;
    }
    if (value == QStringLiteral("integer"))
    {
        return ScriptVariableType::integer;
    }
    if (value == QStringLiteral("boolean"))
    {
        return ScriptVariableType::boolean;
    }
    if (value == QStringLiteral("choice"))
    {
        return ScriptVariableType::choice;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptContinuation> parseScriptContinuation(const QString &value)
{
    using ztermy::workbench::ScriptContinuation;
    if (value == QStringLiteral("immediate"))
    {
        return ScriptContinuation::immediate;
    }
    if (value == QStringLiteral("literal-output"))
    {
        return ScriptContinuation::literalOutput;
    }
    return std::nullopt;
}

[[nodiscard]] QString scriptExecutionStateToken(const ztermy::workbench::ScriptExecutionState state)
{
    using ztermy::workbench::ScriptExecutionState;
    switch (state)
    {
        case ScriptExecutionState::running:
            return QStringLiteral("running");
        case ScriptExecutionState::waitingForOutput:
            return QStringLiteral("waiting-output");
        case ScriptExecutionState::completed:
            return QStringLiteral("completed");
        case ScriptExecutionState::cancelled:
            return QStringLiteral("cancelled");
        case ScriptExecutionState::timedOut:
            return QStringLiteral("timed-out");
        case ScriptExecutionState::idle:
        default:
            return QStringLiteral("idle");
    }
}

[[nodiscard]] QString scriptRenderErrorToken(const ztermy::workbench::ScriptRenderError error)
{
    using ztermy::workbench::ScriptRenderError;
    switch (error)
    {
        case ScriptRenderError::missingVariable:
            return QStringLiteral("missing-variable");
        case ScriptRenderError::invalidVariableValue:
            return QStringLiteral("invalid-variable-value");
        case ScriptRenderError::invalidTemplate:
            return QStringLiteral("invalid-template");
        case ScriptRenderError::unknownTemplateVariable:
            return QStringLiteral("unknown-template-variable");
        case ScriptRenderError::renderedTooLarge:
            return QStringLiteral("rendered-too-large");
        case ScriptRenderError::invalidDefinition:
        default:
            return QStringLiteral("invalid-definition");
    }
}

[[nodiscard]] QString shellKindToken(const ztermy::workbench::ShellKind value)
{
    switch (value)
    {
        case ztermy::workbench::ShellKind::bash:
            return QStringLiteral("bash");
        case ztermy::workbench::ShellKind::zsh:
            return QStringLiteral("zsh");
        case ztermy::workbench::ShellKind::fish:
            return QStringLiteral("fish");
        case ztermy::workbench::ShellKind::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellKind::unknown:
        default:
            return QStringLiteral("unknown");
    }
}

[[nodiscard]] QString normalizedQuickCommandText(QString value)
{
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return value;
}

[[nodiscard]] bool validTerminalCommand(const QString &command)
{
    constexpr qsizetype maximumCommandCharacters = qsizetype{64} * 1024;
    if (command.trimmed().isEmpty() || command.size() > maximumCommandCharacters)
    {
        return false;
    }
    return std::ranges::none_of(command, [](const QChar character) {
        const ushort value = character.unicode();
        return (value < 0x20U && value != '\t' && value != '\n' && value != '\r') || value == 0x7FU;
    });
}

[[nodiscard]] bool reservedAiQuickMessageSlug(const std::string_view slug) noexcept
{
    constexpr std::array reserved{std::string_view{"new"},     std::string_view{"history"},
                                  std::string_view{"explain"}, std::string_view{"selection"},
                                  std::string_view{"last"},    std::string_view{"last3"},
                                  std::string_view{"last5"},   std::string_view{"command"}};
    return std::ranges::find(reserved, slug) != reserved.end();
}

constexpr std::size_t maximumHistoryEntries = 1000;
constexpr qsizetype maximumPendingHistoryBytes = qsizetype{64} * 1024;

void removeLastUtf8CodePoint(QByteArray &value)
{
    if (value.isEmpty())
    {
        return;
    }
    qsizetype index = value.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(value.at(index)) & 0xC0U) == 0x80U)
    {
        --index;
    }
    value.truncate(index);
}

[[nodiscard]] QVariantMap terminalHistoryValue(const ztermy::workbench::ShellHistoryEntry &entry,
                                               const QString &sourceLabel = {}, const QString &sourceId = {})
{
    QVariantMap value{
        {QStringLiteral("command"), utf8QString(entry.command)},
        {QStringLiteral("shell"), shellKindToken(entry.shell)},
    };
    if (entry.timestampUtcSeconds)
    {
        value.insert(QStringLiteral("timestampUtcMs"), *entry.timestampUtcSeconds * 1000);
    }
    if (!sourceLabel.isEmpty())
    {
        value.insert(QStringLiteral("sourceLabel"), sourceLabel);
    }
    if (!sourceId.isEmpty())
    {
        value.insert(QStringLiteral("sourceId"), sourceId);
    }
    return value;
}

[[nodiscard]] bool profileHasStoredCredential(const ztermy::ssh::SshProfile &profile,
                                              const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    if (profile.authentication == ztermy::ssh::SshAuthenticationMethod::Agent)
    {
        return false;
    }
    if (!profile.credentialReference)
    {
        return false;
    }
    if (availableKeys == nullptr)
    {
        // A locked or temporarily unavailable persistent backend cannot be
        // enumerated. Preserve the non-secret profile metadata so the UI can
        // offer the appropriate unlock/recovery flow.
        return true;
    }
    const ztermy::security::CredentialKey key{
        .profileId = *profile.credentialReference,
        .kind = profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
                    ? ztermy::security::CredentialKind::PrivateKeyPassphrase
                    : ztermy::security::CredentialKind::Password,
    };
    return std::ranges::find(*availableKeys, key) != availableKeys->end();
}

[[nodiscard]] bool profileHasStoredProxyCredential(const ztermy::ssh::SshProfile &profile,
                                                   const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    if (!profile.proxy.credentialReference)
    {
        return false;
    }
    if (availableKeys == nullptr)
    {
        return true;
    }
    const ztermy::security::CredentialKey key{
        .profileId = *profile.proxy.credentialReference,
        .kind = ztermy::security::CredentialKind::ProxyPassword,
    };
    return std::ranges::find(*availableKeys, key) != availableKeys->end();
}

[[nodiscard]] QVariantMap sessionOptionsVariantMap(const ztermy::ssh::SshSessionOptions &options)
{
    QVariantList environment;
    environment.reserve(static_cast<qsizetype>(options.environment.size()));
    for (const ztermy::ssh::SshEnvironmentVariable &variable : options.environment)
    {
        environment.push_back(QVariantMap{{QStringLiteral("name"), utf8QString(variable.name)},
                                          {QStringLiteral("value"), utf8QString(variable.value)}});
    }
    return {
        {QStringLiteral("terminalType"), utf8QString(options.terminalType)},
        {QStringLiteral("connectionTimeoutSeconds"), options.connectionTimeoutSeconds},
        {QStringLiteral("keepaliveIntervalSeconds"), options.keepaliveIntervalSeconds},
        {QStringLiteral("keepaliveFailureThreshold"), options.keepaliveFailureThreshold},
        {QStringLiteral("startupCommand"), utf8QString(options.startupCommand)},
        {QStringLiteral("startupCommandMode"),
         options.startupCommandMode == ztermy::ssh::SshStartupCommandMode::LineDelay ? QStringLiteral("line-delay")
                                                                                     : QStringLiteral("paste")},
        {QStringLiteral("startupLineDelayMilliseconds"), options.startupLineDelayMilliseconds},
        {QStringLiteral("environment"), environment},
        {QStringLiteral("reconnectPolicy"),
         options.reconnectPolicy == ztermy::ssh::SshReconnectPolicy::OnTransportFailure
             ? QStringLiteral("transport-failure")
             : QStringLiteral("never")},
        {QStringLiteral("reconnectMaximumAttempts"), options.reconnectMaximumAttempts},
        {QStringLiteral("reconnectInitialBackoffMilliseconds"), options.reconnectInitialBackoffMilliseconds},
    };
}

[[nodiscard]] std::optional<ztermy::ssh::SshSessionOptions> mergeSessionOptions(const QVariantMap &overrides,
                                                                                ztermy::ssh::SshSessionOptions options)
{
    const auto integerOverride = [&overrides](const QString &key, auto &target) {
        using Target = std::remove_cvref_t<decltype(target)>;
        if (!overrides.contains(key))
        {
            return true;
        }
        bool valid = false;
        const int value = overrides.value(key).toInt(&valid);
        if (!valid || value < 0 || std::cmp_greater(value, (std::numeric_limits<Target>::max)()))
        {
            return false;
        }
        target = static_cast<Target>(value);
        return true;
    };

    if (overrides.contains(QStringLiteral("terminalType")))
    {
        options.terminalType = utf8String(overrides.value(QStringLiteral("terminalType")).toString().trimmed());
    }
    if (overrides.contains(QStringLiteral("startupCommand")))
    {
        options.startupCommand = utf8String(overrides.value(QStringLiteral("startupCommand")).toString());
    }
    if (overrides.contains(QStringLiteral("startupCommandMode")))
    {
        const QString mode = overrides.value(QStringLiteral("startupCommandMode")).toString();
        if (mode == QStringLiteral("paste"))
        {
            options.startupCommandMode = ztermy::ssh::SshStartupCommandMode::Paste;
        }
        else if (mode == QStringLiteral("line-delay"))
        {
            options.startupCommandMode = ztermy::ssh::SshStartupCommandMode::LineDelay;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (overrides.contains(QStringLiteral("reconnectPolicy")))
    {
        const QString policy = overrides.value(QStringLiteral("reconnectPolicy")).toString();
        if (policy == QStringLiteral("never"))
        {
            options.reconnectPolicy = ztermy::ssh::SshReconnectPolicy::Never;
        }
        else if (policy == QStringLiteral("transport-failure"))
        {
            options.reconnectPolicy = ztermy::ssh::SshReconnectPolicy::OnTransportFailure;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!integerOverride(QStringLiteral("connectionTimeoutSeconds"), options.connectionTimeoutSeconds)
        || !integerOverride(QStringLiteral("keepaliveIntervalSeconds"), options.keepaliveIntervalSeconds)
        || !integerOverride(QStringLiteral("keepaliveFailureThreshold"), options.keepaliveFailureThreshold)
        || !integerOverride(QStringLiteral("startupLineDelayMilliseconds"), options.startupLineDelayMilliseconds)
        || !integerOverride(QStringLiteral("reconnectMaximumAttempts"), options.reconnectMaximumAttempts)
        || !integerOverride(QStringLiteral("reconnectInitialBackoffMilliseconds"),
                            options.reconnectInitialBackoffMilliseconds))
    {
        return std::nullopt;
    }
    if (overrides.contains(QStringLiteral("environment")))
    {
        const QVariantList values = overrides.value(QStringLiteral("environment")).toList();
        options.environment.clear();
        options.environment.reserve(static_cast<std::size_t>(values.size()));
        for (const QVariant &value : values)
        {
            const QVariantMap variable = value.toMap();
            if (!variable.contains(QStringLiteral("name")) || !variable.contains(QStringLiteral("value")))
            {
                return std::nullopt;
            }
            options.environment.push_back(
                {.name = utf8String(variable.value(QStringLiteral("name")).toString().trimmed()),
                 .value = utf8String(variable.value(QStringLiteral("value")).toString())});
        }
    }
    return ztermy::ssh::validSshSessionOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] QVariantMap proxyOptionsVariantMap(const ztermy::ssh::SshProxyOptions &options,
                                                 const bool credentialStored)
{
    return {
        {QStringLiteral("type"), proxyTypeToken(options.type)},
        {QStringLiteral("host"), utf8QString(options.host)},
        {QStringLiteral("port"), options.port},
        {QStringLiteral("username"), utf8QString(options.username)},
        {QStringLiteral("credentialStored"), credentialStored},
    };
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyOptions> mergeProxyOptions(const QVariantMap &overrides,
                                                                            ztermy::ssh::SshProxyOptions options)
{
    if (overrides.contains(QStringLiteral("type")))
    {
        const auto type = parseProxyTypeToken(overrides.value(QStringLiteral("type")).toString());
        if (!type)
        {
            return std::nullopt;
        }
        options.type = *type;
    }
    if (options.type == ztermy::ssh::SshProxyType::None)
    {
        return ztermy::ssh::SshProxyOptions{};
    }
    if (overrides.contains(QStringLiteral("host")))
    {
        options.host = utf8String(overrides.value(QStringLiteral("host")).toString().trimmed());
    }
    if (overrides.contains(QStringLiteral("port")))
    {
        bool valid = false;
        const int port = overrides.value(QStringLiteral("port")).toInt(&valid);
        if (!valid || port <= 0 || port > 65535)
        {
            return std::nullopt;
        }
        options.port = static_cast<std::uint16_t>(port);
    }
    if (overrides.contains(QStringLiteral("username")))
    {
        options.username = utf8String(overrides.value(QStringLiteral("username")).toString().trimmed());
        if (options.username.empty())
        {
            options.credentialReference.reset();
        }
    }
    return ztermy::ssh::validSshProxyOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
mergeJumpProfileIds(const QVariantMap &routeOptions, std::vector<std::string> jumpProfileIds,
                    const std::string &profileId, const std::vector<ztermy::ssh::SshProfile> &profiles)
{
    const QString key = QStringLiteral("jumpProfileIds");
    if (!routeOptions.contains(key))
    {
        return jumpProfileIds;
    }
    const QVariant value = routeOptions.value(key);
    if (!value.canConvert<QVariantList>())
    {
        return std::nullopt;
    }
    const QVariantList values = value.toList();
    if (values.size() > static_cast<qsizetype>(ztermy::ssh::maximumSshJumpHostCount))
    {
        return std::nullopt;
    }
    jumpProfileIds.clear();
    jumpProfileIds.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant &entry : values)
    {
        const std::string jumpId = utf8String(entry.toString().trimmed());
        if (jumpId.empty() || jumpId == profileId || std::ranges::find(jumpProfileIds, jumpId) != jumpProfileIds.end()
            || std::ranges::find(profiles, jumpId, &ztermy::ssh::SshProfile::id) == profiles.end())
        {
            return std::nullopt;
        }
        jumpProfileIds.push_back(jumpId);
    }
    return jumpProfileIds;
}

[[nodiscard]] bool profileRequiresCredential(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.authentication == ztermy::ssh::SshAuthenticationMethod::Password
           || (profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
               && profile.privateKeyPassphraseRequired);
}

[[nodiscard]] bool proxyRequiresCredential(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.proxy.type != ztermy::ssh::SshProxyType::None && !profile.proxy.username.empty();
}

[[nodiscard]] ztermy::security::CredentialKind credentialKind(const ztermy::ssh::SshProfile &profile) noexcept;

[[nodiscard]] std::expected<std::vector<ztermy::ssh::SshJumpHostRequest>, ztermy::sftp::TransferCredentialError>
storedJumpHostRequests(const ztermy::ssh::SshProfile &profile, const std::vector<ztermy::ssh::SshProfile> &profiles,
                       ztermy::security::CredentialVault &vault) noexcept
{
    using ztermy::sftp::TransferCredentialError;
    try
    {
        std::vector<ztermy::ssh::SshJumpHostRequest> requests;
        requests.reserve(profile.jumpProfileIds.size());
        for (const std::string &jumpProfileId : profile.jumpProfileIds)
        {
            const auto jump = std::ranges::find(profiles, jumpProfileId, &ztermy::ssh::SshProfile::id);
            if (jump == profiles.end())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            ztermy::security::SensitiveByteArray secret;
            if (jump->credentialReference)
            {
                auto stored = vault.read({.profileId = *jump->credentialReference, .kind = credentialKind(*jump)});
                if (!stored)
                {
                    return std::unexpected(stored.error() == ztermy::security::CredentialVaultError::Locked
                                               ? TransferCredentialError::Locked
                                               : TransferCredentialError::Unavailable);
                }
                secret = std::move(*stored);
            }
            if (profileRequiresCredential(*jump) && secret.empty())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            ztermy::security::SensitiveByteArray proxySecret;
            if (jump->proxy.credentialReference)
            {
                auto stored = vault.read({.profileId = *jump->proxy.credentialReference,
                                          .kind = ztermy::security::CredentialKind::ProxyPassword});
                if (!stored)
                {
                    return std::unexpected(stored.error() == ztermy::security::CredentialVaultError::Locked
                                               ? TransferCredentialError::Locked
                                               : TransferCredentialError::Unavailable);
                }
                proxySecret = std::move(*stored);
            }
            if (proxyRequiresCredential(*jump) && proxySecret.empty())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            requests.push_back({
                .profileId = utf8QString(jump->id),
                .displayName = utf8QString(jump->name),
                .host = utf8QString(jump->host),
                .port = jump->port,
                .username = utf8QString(jump->username),
                .authentication = jump->authentication,
                .privateKeyPath = utf8QString(jump->privateKeyPath),
                .secret = std::move(secret),
                .proxy = jump->proxy,
                .proxySecret = std::move(proxySecret),
            });
        }
        return requests;
    }
    catch (...)
    {
        return std::unexpected(TransferCredentialError::Unavailable);
    }
}

[[nodiscard]] QVariantMap profileVariantMap(const ztermy::ssh::SshProfile &profile,
                                            const std::vector<ztermy::ssh::SshProfile> &profiles,
                                            const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    const bool credentialStored = profileHasStoredCredential(profile, availableKeys);
    const bool proxyCredentialStored = profileHasStoredProxyCredential(profile, availableKeys);
    QVariantList jumpProfileIds;
    QVariantList jumpProfiles;
    bool jumpProfilesReady = true;
    bool connectionCredentialStored = credentialStored || proxyCredentialStored;
    jumpProfileIds.reserve(static_cast<qsizetype>(profile.jumpProfileIds.size()));
    jumpProfiles.reserve(static_cast<qsizetype>(profile.jumpProfileIds.size()));
    for (const std::string &jumpId : profile.jumpProfileIds)
    {
        jumpProfileIds.append(utf8QString(jumpId));
        const auto jump = std::ranges::find(profiles, jumpId, &ztermy::ssh::SshProfile::id);
        if (jump == profiles.end())
        {
            jumpProfilesReady = false;
            continue;
        }
        const bool jumpCredentialStored = profileHasStoredCredential(*jump, availableKeys);
        const bool jumpProxyCredentialStored = profileHasStoredProxyCredential(*jump, availableKeys);
        const bool ready = (!profileRequiresCredential(*jump) || jumpCredentialStored)
                           && (!proxyRequiresCredential(*jump) || jumpProxyCredentialStored);
        jumpProfilesReady = jumpProfilesReady && ready;
        connectionCredentialStored = connectionCredentialStored || jumpCredentialStored || jumpProxyCredentialStored;
        jumpProfiles.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(jump->id)},
            {QStringLiteral("name"), utf8QString(jump->name)},
            {QStringLiteral("endpoint"),
             QStringLiteral("%1@%2:%3").arg(utf8QString(jump->username), utf8QString(jump->host)).arg(jump->port)},
            {QStringLiteral("ready"), ready},
        });
    }
    QVariantMap result{
        {QStringLiteral("id"), utf8QString(profile.id)},
        {QStringLiteral("name"), utf8QString(profile.name)},
        {QStringLiteral("group"), utf8QString(profile.group)},
        {QStringLiteral("host"), utf8QString(profile.host)},
        {QStringLiteral("port"), profile.port},
        {QStringLiteral("username"), utf8QString(profile.username)},
        {QStringLiteral("authentication"), authenticationToken(profile.authentication)},
        {QStringLiteral("privateKeyPath"), utf8QString(profile.privateKeyPath)},
        {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
        {QStringLiteral("credentialStored"), credentialStored},
        {QStringLiteral("sessionOptions"), sessionOptionsVariantMap(profile.sessionOptions)},
        {QStringLiteral("proxy"), proxyOptionsVariantMap(profile.proxy, proxyCredentialStored)},
        {QStringLiteral("jumpProfileIds"), jumpProfileIds},
        {QStringLiteral("jumpProfiles"), jumpProfiles},
        {QStringLiteral("jumpProfilesReady"), jumpProfilesReady},
        {QStringLiteral("connectionCredentialStored"), connectionCredentialStored},
    };
    if (profile.lastConnectedUtcMs)
    {
        result.insert(QStringLiteral("lastConnectedUtcMs"), *profile.lastConnectedUtcMs);
    }
    return result;
}

[[nodiscard]] ztermy::security::CredentialStorage
defaultCredentialStorage(const ztermy::config::StorageMode mode) noexcept
{
    return mode == ztermy::config::StorageMode::installed ? ztermy::security::CredentialStorage::System
                                                          : ztermy::security::CredentialStorage::Portable;
}

[[nodiscard]] std::optional<ztermy::security::CredentialStorage> parseCredentialStorage(const QString &value)
{
    if (value == QStringLiteral("system"))
    {
        return ztermy::security::CredentialStorage::System;
    }
    if (value == QStringLiteral("portable"))
    {
        return ztermy::security::CredentialStorage::Portable;
    }
    if (value == QStringLiteral("session"))
    {
        return ztermy::security::CredentialStorage::Session;
    }
    return std::nullopt;
}

[[nodiscard]] ztermy::config::CredentialStoragePreference
credentialPreferenceForStorage(const ztermy::security::CredentialStorage storage) noexcept
{
    switch (storage)
    {
        case ztermy::security::CredentialStorage::System:
            return ztermy::config::CredentialStoragePreference::system;
        case ztermy::security::CredentialStorage::Portable:
            return ztermy::config::CredentialStoragePreference::portable;
        case ztermy::security::CredentialStorage::Session:
            return ztermy::config::CredentialStoragePreference::session;
    }
    return ztermy::config::CredentialStoragePreference::automatic;
}

[[nodiscard]] QString credentialVaultErrorMessage(const ztermy::security::CredentialVaultError error)
{
    using enum ztermy::security::CredentialVaultError;
    switch (error)
    {
        case Locked:
            return QCoreApplication::translate("AppController", "Unlock the portable credential vault first.");
        case Unavailable:
            return QCoreApplication::translate("AppController",
                                               "The selected credential store is unavailable in this Windows session.");
        case AccessDenied:
            return QCoreApplication::translate("AppController",
                                               "Windows denied access to the selected credential store.");
        case AuthenticationFailed:
            return QCoreApplication::translate("AppController",
                                               "The vault password is incorrect, or the vault was modified.");
        case WeakMasterPassword:
            return QCoreApplication::translate("AppController", "Use a vault password with at least 8 UTF-8 bytes.");
        case AlreadyInitialized:
            return QCoreApplication::translate("AppController",
                                               "The portable credential vault is already initialized.");
        case CorruptData:
            return QCoreApplication::translate("AppController", "The portable credential vault is damaged or invalid.");
        case UnsupportedVersion:
            return QCoreApplication::translate("AppController",
                                               "This credential vault was created by an unsupported ztermy version.");
        case EmptySecret:
            return QCoreApplication::translate("AppController", "Enter a password or key passphrase before saving it.");
        case SecretTooLarge:
            return QCoreApplication::translate("AppController", "The credential is larger than the supported limit.");
        case NotFound:
            return QCoreApplication::translate("AppController", "No saved credential was found for this host.");
        case InvalidKey:
            return QCoreApplication::translate("AppController", "The credential reference is invalid.");
        case IoError:
            return QCoreApplication::translate("AppController", "The credential store could not be read or written.");
        case CryptoError:
            return QCoreApplication::translate("AppController",
                                               "Windows could not complete the credential encryption operation.");
        case MigrationFailed:
            return QCoreApplication::translate("AppController",
                                               "Credential migration was rolled back because verification failed.");
    }
    return QCoreApplication::translate("AppController", "The credential operation failed.");
}

void logCredentialRollbackResult(std::expected<void, ztermy::security::CredentialVaultError> result,
                                 const char *operation)
{
    if (!result)
    {
        qCWarning(appControllerLog) << "Credential rollback failed during" << operation
                                    << "error=" << static_cast<int>(result.error());
    }
}

class CredentialMutation final
{
public:
    explicit CredentialMutation(ztermy::security::CredentialVault &vault) noexcept : m_vault(vault) {}

    ~CredentialMutation()
    {
        if (m_active)
        {
            rollback();
        }
    }

    CredentialMutation(const CredentialMutation &) = delete;
    CredentialMutation &operator=(const CredentialMutation &) = delete;

    [[nodiscard]] std::expected<void, ztermy::security::CredentialVaultError>
    apply(const ztermy::security::CredentialKey &desiredKey,
          const std::optional<ztermy::security::CredentialKey> &previousKey, const bool shouldStore,
          const bool shouldRemovePrevious, ztermy::security::SensitiveByteArray secret)
    {
        if (shouldStore)
        {
            auto previous = m_vault.read(desiredKey);
            if (previous)
            {
                m_previousDesiredSecret = std::move(*previous);
            }
            else if (previous.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                return std::unexpected(previous.error());
            }
            auto stored = m_vault.store(desiredKey, std::move(secret));
            if (!stored)
            {
                return stored;
            }
            m_desiredKey = desiredKey;
        }

        if (shouldRemovePrevious && previousKey && (!m_desiredKey || *previousKey != *m_desiredKey))
        {
            auto previous = m_vault.read(*previousKey);
            if (previous)
            {
                m_removedPreviousSecret = std::move(*previous);
            }
            else if (previous.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                rollback();
                return std::unexpected(previous.error());
            }
            auto removed = m_vault.remove(*previousKey);
            if (!removed && removed.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                rollback();
                return removed;
            }
            if (m_removedPreviousSecret)
            {
                m_removedKey = *previousKey;
            }
        }
        return {};
    }

    void commit() noexcept
    {
        m_active = false;
        m_previousDesiredSecret.reset();
        m_removedPreviousSecret.reset();
    }

private:
    void rollback() noexcept
    {
        m_active = false;
        try
        {
            if (m_removedKey && m_removedPreviousSecret)
            {
                const std::string_view bytes = m_removedPreviousSecret->view();
                logCredentialRollbackResult(
                    m_vault.store(*m_removedKey, ztermy::security::SensitiveByteArray(
                                                     QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                    "previous credential restore");
            }
            if (m_desiredKey)
            {
                if (m_previousDesiredSecret)
                {
                    const std::string_view bytes = m_previousDesiredSecret->view();
                    logCredentialRollbackResult(
                        m_vault.store(*m_desiredKey, ztermy::security::SensitiveByteArray(QByteArray(
                                                         bytes.data(), static_cast<qsizetype>(bytes.size())))),
                        "profile-save restore");
                }
                else
                {
                    logCredentialRollbackResult(m_vault.remove(*m_desiredKey), "profile-save removal");
                }
            }
        }
        catch (...)
        {
            qCWarning(appControllerLog) << "Credential rollback raised an unexpected exception";
        }
    }

    ztermy::security::CredentialVault &m_vault;
    std::optional<ztermy::security::CredentialKey> m_desiredKey;
    std::optional<ztermy::security::CredentialKey> m_removedKey;
    std::optional<ztermy::security::SensitiveByteArray> m_previousDesiredSecret;
    std::optional<ztermy::security::SensitiveByteArray> m_removedPreviousSecret;
    bool m_active = true;
};

[[nodiscard]] ztermy::security::CredentialKind credentialKind(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.authentication == ztermy::ssh::SshAuthenticationMethod::Password
               ? ztermy::security::CredentialKind::Password
               : ztermy::security::CredentialKind::PrivateKeyPassphrase;
}

[[nodiscard]] QString transferStatusToken(const ztermy::sftp::TransferStatus status)
{
    using ztermy::sftp::TransferStatus;
    switch (status)
    {
        case TransferStatus::Queued:
            return QStringLiteral("queued");
        case TransferStatus::Running:
            return QStringLiteral("running");
        case TransferStatus::Pausing:
            return QStringLiteral("pausing");
        case TransferStatus::Paused:
            return QStringLiteral("paused");
        case TransferStatus::Cancelling:
            return QStringLiteral("cancelling");
        case TransferStatus::NeedsAttention:
            return QStringLiteral("needs-attention");
        case TransferStatus::Completed:
            return QStringLiteral("completed");
        case TransferStatus::Failed:
            return QStringLiteral("failed");
        case TransferStatus::Cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString transferBatchStatusToken(const ztermy::sftp::TransferBatchStatus status)
{
    using ztermy::sftp::TransferBatchStatus;
    switch (status)
    {
        case TransferBatchStatus::Discovering:
            return QStringLiteral("discovering");
        case TransferBatchStatus::Ready:
            return QStringLiteral("ready");
        case TransferBatchStatus::Running:
            return QStringLiteral("running");
        case TransferBatchStatus::Paused:
            return QStringLiteral("paused");
        case TransferBatchStatus::NeedsAttention:
            return QStringLiteral("needs-attention");
        case TransferBatchStatus::Completed:
            return QStringLiteral("completed");
        case TransferBatchStatus::Failed:
            return QStringLiteral("failed");
        case TransferBatchStatus::Cancelled:
            return QStringLiteral("cancelled");
        case TransferBatchStatus::Interrupted:
            return QStringLiteral("interrupted");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString sessionLogStateToken(const ztermy::logging::SessionLogState state)
{
    using ztermy::logging::SessionLogState;
    switch (state)
    {
        case SessionLogState::Idle:
            return QStringLiteral("idle");
        case SessionLogState::Starting:
            return QStringLiteral("starting");
        case SessionLogState::Active:
            return QStringLiteral("active");
        case SessionLogState::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString transferErrorMessage(const std::string &errorCode)
{
    const QString code = utf8QString(errorCode);
    if (code == QStringLiteral("interrupted"))
    {
        return QCoreApplication::translate("AppController",
                                           "Interrupted by the previous shutdown. Retry to start the transfer again.");
    }
    if (code == QStringLiteral("credential-locked"))
    {
        return QCoreApplication::translate("AppController", "Unlock the credential vault, then retry the transfer.");
    }
    if (code == QStringLiteral("credential-unavailable") || code == QStringLiteral("authentication-unavailable"))
    {
        return QCoreApplication::translate("AppController", "Review the saved authentication method, then retry.");
    }
    if (code == QStringLiteral("authentication-rejected"))
    {
        return QCoreApplication::translate("AppController",
                                           "Authentication was rejected. Update the credential, then retry.");
    }
    if (code == QStringLiteral("remote-timeout"))
    {
        return QCoreApplication::translate("AppController",
                                           "The remote operation timed out. Check connectivity, then retry.");
    }
    if (code == QStringLiteral("remote-connection-lost"))
    {
        return QCoreApplication::translate("AppController", "The remote connection was lost. Reconnect, then retry.");
    }
    if (code == QStringLiteral("local-io") || code == QStringLiteral("commit-failed"))
    {
        return QCoreApplication::translate(
            "AppController", "The local file could not be written. Check the path, permissions, and free space.");
    }
    if (code == QStringLiteral("remote-io"))
    {
        return QCoreApplication::translate(
            "AppController", "The remote file operation failed. Check the path and permissions, then retry.");
    }
    if (code == QStringLiteral("file-conflict"))
    {
        return QCoreApplication::translate("AppController", "Choose whether to replace, rename, skip, or cancel.");
    }
    if (code == QStringLiteral("incompatible-conflict") || code == QStringLiteral("invalid-conflict-rename"))
    {
        return QCoreApplication::translate("AppController", "Choose a compatible destination name and try again.");
    }
    if (code == QStringLiteral("invalid-task"))
    {
        return QCoreApplication::translate("AppController", "The source or destination is no longer valid.");
    }
    if (code == QStringLiteral("credential-cancelled") || code == QStringLiteral("cancelled"))
    {
        return QCoreApplication::translate("AppController", "Authentication was cancelled.");
    }
    if (code == QStringLiteral("worker-initialization-failed"))
    {
        return QCoreApplication::translate("AppController", "The transfer worker could not start. Retry the transfer.");
    }
    return code.isEmpty() ? QString{}
                          : QCoreApplication::translate(
                                "AppController", "The transfer failed. Review the paths and connection, then retry.");
}

[[nodiscard]] QVariantMap transferTaskValue(const ztermy::sftp::TransferTask &task)
{
    return {
        {QStringLiteral("id"), utf8QString(task.id)},
        {QStringLiteral("endpointId"), utf8QString(task.endpointId)},
        {QStringLiteral("displayName"), utf8QString(task.displayName)},
        {QStringLiteral("sourcePath"), utf8QString(task.sourcePath)},
        {QStringLiteral("destinationPath"), utf8QString(task.destinationPath)},
        {QStringLiteral("filenameEncoding"), utf8QString(task.filenameEncoding)},
        {QStringLiteral("direction"), task.direction == ztermy::sftp::TransferDirection::Download
                                          ? QStringLiteral("download")
                                          : QStringLiteral("upload")},
        {QStringLiteral("status"), transferStatusToken(task.status)},
        {QStringLiteral("totalBytes"), QVariant::fromValue<qulonglong>(task.totalBytes)},
        {QStringLiteral("transferredBytes"), QVariant::fromValue<qulonglong>(task.transferredBytes)},
        {QStringLiteral("bytesPerSecond"), QVariant::fromValue<qulonglong>(task.bytesPerSecond)},
        {QStringLiteral("errorCode"), utf8QString(task.errorCode)},
        {QStringLiteral("errorMessage"), transferErrorMessage(task.errorCode)},
        {QStringLiteral("retryable"), task.retryable},
    };
}

[[nodiscard]] QVariantMap transferBatchValue(const ztermy::sftp::TransferBatch &batch)
{
    const ztermy::sftp::TransferBatchSummary summary = ztermy::sftp::summarizeTransferBatch(batch);
    QStringList roots;
    roots.reserve(static_cast<qsizetype>(batch.sourceRoots.size()));
    for (const std::string &root : batch.sourceRoots)
    {
        roots.push_back(utf8QString(root));
    }
    return {
        {QStringLiteral("id"), utf8QString(batch.id)},
        {QStringLiteral("endpointId"), utf8QString(batch.endpointId)},
        {QStringLiteral("displayName"), utf8QString(batch.displayName)},
        {QStringLiteral("sourceRoots"), roots},
        {QStringLiteral("destinationRoot"), utf8QString(batch.destinationRoot)},
        {QStringLiteral("direction"), batch.direction == ztermy::sftp::TransferBatchDirection::Download
                                          ? QStringLiteral("download")
                                          : QStringLiteral("upload")},
        {QStringLiteral("status"), transferBatchStatusToken(batch.status)},
        {QStringLiteral("entryCount"), QVariant::fromValue<qulonglong>(summary.entryCount)},
        {QStringLiteral("directoryCount"), QVariant::fromValue<qulonglong>(summary.directoryCount)},
        {QStringLiteral("fileCount"), QVariant::fromValue<qulonglong>(summary.regularFileCount)},
        {QStringLiteral("completedCount"), QVariant::fromValue<qulonglong>(summary.completedCount)},
        {QStringLiteral("skippedCount"), QVariant::fromValue<qulonglong>(summary.skippedCount)},
        {QStringLiteral("failedCount"), QVariant::fromValue<qulonglong>(summary.failedCount)},
        {QStringLiteral("totalBytes"), QVariant::fromValue<qulonglong>(summary.totalBytes)},
        {QStringLiteral("transferredBytes"), QVariant::fromValue<qulonglong>(summary.transferredBytes)},
        {QStringLiteral("bytesPerSecond"), QVariant::fromValue<qulonglong>(batch.bytesPerSecond)},
        {QStringLiteral("errorCode"), utf8QString(batch.discoveryErrorCode)},
        {QStringLiteral("errorMessage"), transferErrorMessage(batch.discoveryErrorCode)},
    };
}

[[nodiscard]] QString quickConnectError(const ztermy::ssh::SshTargetError error)
{
    using enum ztermy::ssh::SshTargetError;
    switch (error)
    {
        case MissingUsername:
            return QCoreApplication::translate("AppController", "Enter a username before @.");
        case MissingHost:
            return QCoreApplication::translate("AppController", "Enter a host after @.");
        case InvalidPort:
            return QCoreApplication::translate("AppController", "Port must be a number from 1 to 65535.");
        case BracketsRequired:
            return QCoreApplication::translate("AppController",
                                               "Wrap an IPv6 host in brackets, for example user@[::1]:22.");
        case InvalidFormat:
            return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
    }
    return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
}

[[nodiscard]] QString localizedSshFailure(const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshFailureKind;
    if (!failure)
    {
        return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
    }
    switch (*failure)
    {
        case NameResolutionFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH host name resolution failed");
        case ConnectionRefused:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection was refused");
        case TimedOut:
            return QCoreApplication::translate("SshTerminalSession", "SSH operation timed out");
        case TransportError:
            return QCoreApplication::translate("SshTerminalSession", "SSH transport failed");
        case HostKeyChanged:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key changed");
        case HostKeyInvalid:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key could not be verified");
        case AuthenticationRejected:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication was rejected");
        case AuthenticationUnavailable:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication method is unavailable");
        case ChannelOpenFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH terminal channel could not be opened");
        case RemoteClosed:
            return QCoreApplication::translate("SshTerminalSession", "SSH remote host closed the connection");
        case Cancelled:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection cancelled");
        case ProtocolError:
            return QCoreApplication::translate("SshTerminalSession", "SSH protocol error");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

[[nodiscard]] QString localizedSshStatus(const ztermy::ssh::SshConnectionPhase phase,
                                         const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Resolving:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Resolving SSH host");
        case Connecting:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Connecting to SSH host");
        case Handshaking:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Negotiating SSH connection");
        case VerifyingHostKey:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Verifying SSH host key");
        case AwaitingHostKeyConfirmation:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH host key confirmation required");
        case Authenticating:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Authenticating SSH session");
        case OpeningChannel:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Opening SSH terminal");
        case Connected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal connected");
        case Failed:
            return localizedSshFailure(failure);
        case Closing:
        case Disconnected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal disconnected");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

[[nodiscard]] QString sshConnectionPhaseToken(const ztermy::ssh::SshConnectionPhase phase)
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Disconnected:
            return QStringLiteral("disconnected");
        case Resolving:
            return QStringLiteral("resolving");
        case Connecting:
            return QStringLiteral("connecting");
        case Handshaking:
            return QStringLiteral("handshaking");
        case VerifyingHostKey:
            return QStringLiteral("verifying-host-key");
        case AwaitingHostKeyConfirmation:
            return QStringLiteral("awaiting-host-key-confirmation");
        case Authenticating:
            return QStringLiteral("authenticating");
        case OpeningChannel:
            return QStringLiteral("opening-channel");
        case Connected:
            return QStringLiteral("connected");
        case Closing:
            return QStringLiteral("closing");
        case Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("disconnected");
}

[[nodiscard]] QString sshConnectionStageToken(const ztermy::ssh::SshConnectionPhase phase)
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Resolving:
        case Connecting:
        case Handshaking:
        case VerifyingHostKey:
        case AwaitingHostKeyConfirmation:
            return QStringLiteral("transport");
        case Authenticating:
            return QStringLiteral("authentication");
        case OpeningChannel:
            return QStringLiteral("terminal");
        case Connected:
            return QStringLiteral("connected");
        case Failed:
            return QStringLiteral("failed");
        case Disconnected:
        case Closing:
            return QStringLiteral("idle");
    }
    return QStringLiteral("idle");
}

[[nodiscard]] int sshConnectionStageIndex(const ztermy::ssh::SshConnectionPhase phase) noexcept
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Resolving:
        case Connecting:
        case Handshaking:
        case VerifyingHostKey:
        case AwaitingHostKeyConfirmation:
            return 0;
        case Authenticating:
            return 1;
        case OpeningChannel:
            return 2;
        case Disconnected:
        case Connected:
        case Closing:
        case Failed:
            return -1;
    }
    return -1;
}

} // namespace

namespace ztermy
{

AppController::AppController(QObject *parent)
    : AppController(applicationDataFile(QStringLiteral("profiles.json")),
                    applicationDataFile(QStringLiteral("known_hosts.json")),
                    applicationDataFile(QStringLiteral("settings.json")), parent)
{
}

AppController::AppController(const QString &profileStorePath, QObject *parent)
    : AppController(profileStorePath, siblingKnownHostsFile(profileStorePath), siblingSettingsFile(profileStorePath),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath),
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, std::move(localSessionFactory),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_aiClipboard(std::make_unique<windowing::WindowsProtectedClipboard>()),
      m_profileStore(std::move(profileStorePath)),
      m_portForwardingStore(siblingPortForwardingFile(m_profileStore.filePath())),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_scriptStore(siblingScriptsFile(m_settingsStore.filePath())),
      m_legacyQuickCommandPath(siblingQuickCommandsFile(m_settingsStore.filePath())),
      m_noteStore(siblingNotesDirectory(m_settingsStore.filePath())),
      m_workspaceStateStore(siblingWorkspaceStateFile(m_settingsStore.filePath())),
      m_aiActivity(siblingAiAuditFile(m_settingsStore.filePath())),
      m_aiPermissionRuleStore(siblingAiPermissionRulesFile(m_settingsStore.filePath())),
      m_aiQuickMessageStore(siblingAiQuickMessagesFile(m_settingsStore.filePath())),
      m_aiUserSkillCatalog(siblingAiUserSkillsDirectory(m_settingsStore.filePath())),
      m_mcpRuntime(siblingMcpServersFile(m_settingsStore.filePath()),
                   [this] {
                       emit mcpConfigurationChanged();
                   }),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          siblingCredentialsFile(m_profileStore.filePath()), security::CredentialStorage::Session)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    qRegisterMetaType<ShellHistoryEntries>();
    qRegisterMetaType<NoteSearchResults>();
    qRegisterMetaType<AiTextAttachments>();
    qRegisterMetaType<AiImageAttachments>();
    qRegisterMetaType<AiUserSkills>();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::terminalWorkspaceChanged);
    QObject::connect(this, &AppController::terminalHistoryTaskCompleted, this,
                     &AppController::applyTerminalHistoryTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::noteSearchTaskCompleted, this, &AppController::applyNoteSearchTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiNoteReadTaskCompleted, this, &AppController::applyAiNoteReadTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiTextAttachmentTaskCompleted, this,
                     &AppController::applyAiTextAttachmentTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiImageAttachmentTaskCompleted, this,
                     &AppController::applyAiImageAttachmentTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiUserSkillTaskCompleted, this, &AppController::applyAiUserSkillTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::scriptOutputObserved, this, &AppController::observeScriptOutput,
                     Qt::QueuedConnection);
    initializeAiPrivacySignals();
    initializePortForwardingSignalBridges();
    loadHostProfiles();
    loadPortForwardingRules();
    loadApplicationSettings();
    loadAiPermissionRules();
    loadAiQuickMessages();
    initializeAiDebugTrace();
    m_mcpRuntime.initialize();
    initializeAiConversationHistory();
    initializeActionRegistry();
    initializeTransferManager();
    initializeScriptExecutionTimer();
    loadQuickCommands();
    loadWorkspaceState();
    refreshNotes();
    initializeAiModelCatalogRefresh();
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath), std::move(credentialsPath),
          storageMode,
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_aiClipboard(std::make_unique<windowing::WindowsProtectedClipboard>()),
      m_profileStore(std::move(profileStorePath)),
      m_portForwardingStore(siblingPortForwardingFile(m_profileStore.filePath())),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_scriptStore(siblingScriptsFile(m_settingsStore.filePath())),
      m_legacyQuickCommandPath(siblingQuickCommandsFile(m_settingsStore.filePath())),
      m_noteStore(siblingNotesDirectory(m_settingsStore.filePath())),
      m_workspaceStateStore(siblingWorkspaceStateFile(m_settingsStore.filePath())),
      m_aiActivity(siblingAiAuditFile(m_settingsStore.filePath())),
      m_aiPermissionRuleStore(siblingAiPermissionRulesFile(m_settingsStore.filePath())),
      m_aiQuickMessageStore(siblingAiQuickMessagesFile(m_settingsStore.filePath())),
      m_aiUserSkillCatalog(siblingAiUserSkillsDirectory(m_settingsStore.filePath())),
      m_mcpRuntime(siblingMcpServersFile(m_settingsStore.filePath()),
                   [this] {
                       emit mcpConfigurationChanged();
                   }),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          credentialsPath.isEmpty() ? siblingCredentialsFile(m_profileStore.filePath()) : std::move(credentialsPath),
          defaultCredentialStorage(storageMode))),
      m_defaultCredentialStorage(defaultCredentialStorage(storageMode)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    Q_ASSERT(m_credentialVaults);
    qRegisterMetaType<ShellHistoryEntries>();
    qRegisterMetaType<NoteSearchResults>();
    qRegisterMetaType<AiTextAttachments>();
    qRegisterMetaType<AiImageAttachments>();
    qRegisterMetaType<AiUserSkills>();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::terminalWorkspaceChanged);
    QObject::connect(this, &AppController::terminalHistoryTaskCompleted, this,
                     &AppController::applyTerminalHistoryTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::noteSearchTaskCompleted, this, &AppController::applyNoteSearchTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiNoteReadTaskCompleted, this, &AppController::applyAiNoteReadTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiTextAttachmentTaskCompleted, this,
                     &AppController::applyAiTextAttachmentTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiImageAttachmentTaskCompleted, this,
                     &AppController::applyAiImageAttachmentTaskResult, Qt::QueuedConnection);
    QObject::connect(this, &AppController::aiUserSkillTaskCompleted, this, &AppController::applyAiUserSkillTaskResult,
                     Qt::QueuedConnection);
    QObject::connect(this, &AppController::scriptOutputObserved, this, &AppController::observeScriptOutput,
                     Qt::QueuedConnection);
    initializeAiPrivacySignals();
    initializePortForwardingSignalBridges();
    loadHostProfiles();
    loadPortForwardingRules();
    loadApplicationSettings();
    loadAiPermissionRules();
    loadAiQuickMessages();
    initializeAiDebugTrace();
    m_mcpRuntime.initialize();
    initializeAiConversationHistory();
    initializeActionRegistry();
    initializeTransferManager();
    initializeScriptExecutionTimer();
    loadQuickCommands();
    loadWorkspaceState();
    refreshNotes();
    initializeAiModelCatalogRefresh();
}

AppController::~AppController()
{
    shutdown();
}

void AppController::initializePortForwardingSignalBridges()
{
    QObject::connect(
        this, &AppController::portForwardingSnapshotReady, this,
        [this](const QString &ruleId, const int state, const int failure, const qulonglong activeClients,
               const qulonglong bytesFromClients, const qulonglong bytesToClients, const qulonglong rejectedClients) {
            applyPortForwardingSnapshot(ruleId.toUtf8().toStdString(),
                                        forwarding::PortForwardingJobSnapshot{
                                            .state = static_cast<forwarding::PortForwardingJobState>(state),
                                            .failure = static_cast<forwarding::PortForwardingJobFailure>(failure),
                                            .activeClients = static_cast<std::size_t>(activeClients),
                                            .bytesFromClients = static_cast<std::uint64_t>(bytesFromClients),
                                            .bytesToClients = static_cast<std::uint64_t>(bytesToClients),
                                            .rejectedClients = static_cast<std::uint64_t>(rejectedClients),
                                        });
        },
        Qt::QueuedConnection);
    QObject::connect(
        this, &AppController::portForwardingHostKeyPromptRequested, this,
        [this](const QString &ruleId, const QString &endpoint, const QString &algorithm, const QString &fingerprint,
               const bool changed) {
            PortForwardingRuntime *runtime = findPortForwardingRuntime(ruleId.toUtf8().toStdString());
            if (runtime == nullptr)
            {
                return;
            }
            if (m_shutdownStarted || m_hostKeyPromptVisible)
            {
                if (!changed)
                {
                    resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
                }
                return;
            }
            m_hostKeyForwardingRuleId = ruleId;
            setHostKeyPrompt(endpoint, algorithm, fingerprint, changed);
        },
        Qt::QueuedConnection);
}

void AppController::attachTerminal(ui::TerminalItem *terminal)
{
    m_terminal = terminal;
    const TerminalTab *tab = activeTab();
    if (terminal == nullptr || tab == nullptr)
    {
        return;
    }
    attachTerminalViewport(tab->paneId, terminal);
}

void AppController::attachTerminalViewport(const QString &paneId, QObject *viewport)
{
    auto *terminal = qobject_cast<ui::TerminalItem *>(viewport);
    const TerminalTab *tab = findTabForPane(paneId);
    if (terminal == nullptr || tab == nullptr)
    {
        return;
    }
    if (const auto existing = m_terminalViewports.value(paneId); existing == terminal)
    {
        showTabInViewport(*tab);
        return;
    }
    QObject::disconnect(terminal, nullptr, this, nullptr);
    m_terminalViewports.insert(paneId, terminal);
    if (tab->id == m_focusedTabId)
    {
        m_terminal = terminal;
    }
    QObject::connect(terminal, &QObject::destroyed, this, [this, paneId, terminal] {
        if (m_terminalViewports.value(paneId) == terminal)
        {
            m_terminalViewports.remove(paneId);
        }
        if (m_terminal == terminal)
        {
            m_terminal = nullptr;
        }
    });
    connectTerminalSignals(*terminal, paneId);
    showTabInViewport(*tab);
}

void AppController::detachTerminalViewport(const QString &paneId, QObject *viewport)
{
    auto *terminal = qobject_cast<ui::TerminalItem *>(viewport);
    if (terminal == nullptr || m_terminalViewports.value(paneId) != terminal)
    {
        return;
    }
    QObject::disconnect(terminal, nullptr, this, nullptr);
    m_terminalViewports.remove(paneId);
    if (m_terminal == terminal)
    {
        m_terminal = nullptr;
    }
}

void AppController::shutdown() noexcept
{
    if (m_shutdownStarted)
    {
        return;
    }
    m_shutdownStarted = true;
    m_scriptExecutionTimer.stop();
    ++m_aiModelsRequestGeneration;
    if (m_aiModelsReply)
    {
        auto *reply = m_aiModelsReply.data();
        m_aiModelsReply = nullptr;
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_aiModelsLoading = false;
    m_aiProviderClient.setTraceHandler({});
    if (m_aiDebugTrace)
    {
        m_aiDebugTrace->stop();
        m_aiDebugTrace.reset();
    }
    m_mcpRuntime.shutdown();
    stopAllPortForwardingRules();
    clearHostKeyPrompt();

    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->requestStop();
    }
    if (m_transferManager)
    {
        m_transferManager->requestStop();
    }
    for (const auto &tab : m_tabs)
    {
        if (tab->sftpSession)
        {
            tab->sftpSession->requestStop();
        }
    }
    for (const auto &session : m_stoppingSftpSessions)
    {
        session->requestStop();
    }

    for (const auto &tab : m_tabs)
    {
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
        if (tab->semanticObserver)
        {
            tab->semanticObserver->finish(terminal::CommandCompletionReason::disconnect);
        }
        if (tab->sessionLog)
        {
            tab->sessionLog->stop();
        }
    }
    m_transferBatchCoordinator.reset();
    if (m_transferManager)
    {
        m_transferManager->shutdown();
        m_transferManager.reset();
    }
    m_tabs.clear();
    m_stoppingSftpSessions.clear();
    m_transferTasks.clear();
    m_transferBatches.clear();
    m_activeTabId.clear();
    m_focusedTabId.clear();
    for (ui::TerminalItem *terminal : std::as_const(m_terminalViewports))
    {
        if (terminal != nullptr)
        {
            QObject::disconnect(terminal, nullptr, this, nullptr);
            terminal->setSnapshot({});
        }
    }
    m_terminalViewports.clear();
    m_terminal = nullptr;
}

bool AppController::sshActive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
}

QString AppController::startupRecoveryNotice() const
{
    return m_startupRecoveryNotice;
}

void AppController::dismissStartupRecoveryNotice()
{
    if (m_startupRecoveryNotice.isEmpty())
    {
        return;
    }
    m_startupRecoveryNotice.clear();
    emit startupRecoveryNoticeChanged();
}

bool AppController::hostKeyPromptVisible() const noexcept
{
    return m_hostKeyPromptVisible;
}

QString AppController::hostKeyEndpoint() const
{
    return m_hostKeyEndpoint;
}

QString AppController::hostKeyAlgorithm() const
{
    return m_hostKeyAlgorithm;
}

QString AppController::hostKeyFingerprint() const
{
    return m_hostKeyFingerprint;
}

bool AppController::hostKeyChangedWarning() const noexcept
{
    return m_hostKeyChangedWarning;
}

QString AppController::defaultPrivateKeyPath() const
{
    return QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                        .filePath(QStringLiteral(".ssh/id_ed25519")));
}

QVariantList AppController::hostProfiles() const
{
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_profiles.size()));
    for (const ssh::SshProfile &profile : m_profiles)
    {
        result.append(profileVariantMap(profile, m_profiles, availableKeys));
    }
    return result;
}

QVariantList AppController::recentHostProfiles() const
{
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    std::vector<const ssh::SshProfile *> recent;
    recent.reserve(m_profiles.size());
    for (const ssh::SshProfile &profile : m_profiles)
    {
        if (profile.lastConnectedUtcMs)
        {
            recent.push_back(&profile);
        }
    }
    std::ranges::sort(recent, std::greater{}, [](const ssh::SshProfile *profile) {
        return *profile->lastConnectedUtcMs;
    });
    if (recent.size() > maximumRecentHostProfiles)
    {
        recent.resize(maximumRecentHostProfiles);
    }

    QVariantList result;
    result.reserve(static_cast<qsizetype>(recent.size()));
    for (const ssh::SshProfile *profile : recent)
    {
        result.append(profileVariantMap(*profile, m_profiles, availableKeys));
    }
    return result;
}

QStringList AppController::hostProfileGroups() const
{
    QStringList groups;
    for (const ssh::SshProfile &profile : m_profiles)
    {
        const QString group = utf8QString(profile.group).trimmed();
        if (!group.isEmpty() && !groups.contains(group, Qt::CaseInsensitive))
        {
            groups.push_back(group);
        }
    }
    std::ranges::sort(groups, [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return groups;
}

QStringList AppController::collapsedHostSections() const
{
    QStringList sections;
    sections.reserve(static_cast<qsizetype>(m_workspaceState.collapsedHostSections.size()));
    for (const std::string &section : m_workspaceState.collapsedHostSections)
    {
        sections.push_back(utf8QString(section));
    }
    return sections;
}

QVariantList AppController::terminalTabs() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_workspaceState.terminalWorkspaces.size()));
    for (std::size_t index = 0; index < m_workspaceState.terminalWorkspaces.size(); ++index)
    {
        const workbench::TerminalWorkspaceLayout &workspace = m_workspaceState.terminalWorkspaces[index];
        const QString representativeId =
            workspace.id == utf8String(m_activeTabId) ? m_focusedTabId : firstTabIdForWorkspace(workspace);
        const TerminalTab *tab = findTab(representativeId);
        if (tab != nullptr)
        {
            QVariantMap value = terminalTabValue(*tab, utf8QString(workspace.id));
            value.insert(QStringLiteral("title"), utf8QString(workspace.title));
            value.insert(QStringLiteral("paneCount"), static_cast<int>(workspace.restoreIntents.size()));
            value.insert(QStringLiteral("tabIndex"), static_cast<int>(index));
            value.insert(QStringLiteral("canDuplicate"),
                         tab->kind == TerminalTabKind::Local || !tab->sourceProfileId.isEmpty());
            value.insert(QStringLiteral("canCloseOthers"), m_workspaceState.terminalWorkspaces.size() > 1U);
            value.insert(QStringLiteral("canCloseToRight"), index + 1U < m_workspaceState.terminalWorkspaces.size());
            value.insert(QStringLiteral("canMoveLeft"), index > 0U);
            value.insert(QStringLiteral("canMoveRight"), index + 1U < m_workspaceState.terminalWorkspaces.size());
            result.append(value);
        }
    }
    return result;
}

bool AppController::canReopenClosedTerminalTab() const noexcept
{
    return !m_closedTerminalTabs.empty();
}

QVariantMap AppController::terminalTabValue(const TerminalTab &tab, const QString &publicId) const
{
    const workbench::ScriptExecutionSnapshot execution = tab.scriptExecution.snapshot();
    const ssh::SshConnectionPhase connectionPhase = tab.sshPhase;
    return {
        {QStringLiteral("id"), publicId},
        {QStringLiteral("sessionId"), tab.id},
        {QStringLiteral("paneId"), tab.paneId},
        {QStringLiteral("title"), tab.title},
        {QStringLiteral("kind"), tab.kind == TerminalTabKind::Local ? QStringLiteral("local") : QStringLiteral("ssh")},
        {QStringLiteral("status"), tab.status},
        {QStringLiteral("identity"), tab.identity.isEmpty() ? tab.title : tab.identity},
        {QStringLiteral("address"), tab.address},
        {QStringLiteral("connectedUtcMs"), tab.connectedUtcMs},
        {QStringLiteral("logState"),
         tab.sessionLog ? sessionLogStateToken(tab.sessionLog->state()) : QStringLiteral("idle")},
        {QStringLiteral("logPath"), tab.sessionLog ? tab.sessionLog->path() : QString{}},
        {QStringLiteral("logError"), tab.sessionLog ? tab.sessionLog->errorString() : QString{}},
        {QStringLiteral("logDroppedBytes"),
         QVariant::fromValue<qulonglong>(tab.sessionLog ? tab.sessionLog->droppedBytes() : 0)},
        {QStringLiteral("running"), tab.running},
        {QStringLiteral("reconnecting"), tab.reconnectPending},
        {QStringLiteral("reconnectAttempt"), static_cast<int>(tab.reconnectAttempt)},
        {QStringLiteral("canReconnect"),
         tab.kind == TerminalTabKind::Ssh && !tab.sourceProfileId.isEmpty() && !tab.running},
        {QStringLiteral("connectionPhase"), sshConnectionPhaseToken(connectionPhase)},
        {QStringLiteral("connectionStage"), sshConnectionStageToken(connectionPhase)},
        {QStringLiteral("connectionStageIndex"), sshConnectionStageIndex(connectionPhase)},
        {QStringLiteral("connectionStageCount"), 3},
        {QStringLiteral("connectionInteractionRequired"),
         connectionPhase == ssh::SshConnectionPhase::AwaitingHostKeyConfirmation},
        {QStringLiteral("connected"),
         tab.kind == TerminalTabKind::Ssh && connectionPhase == ssh::SshConnectionPhase::Connected},
        {QStringLiteral("connecting"), tab.kind == TerminalTabKind::Ssh
                                           && connectionPhase != ssh::SshConnectionPhase::Disconnected
                                           && connectionPhase != ssh::SshConnectionPhase::Connected
                                           && connectionPhase != ssh::SshConnectionPhase::Closing
                                           && connectionPhase != ssh::SshConnectionPhase::Failed},
        {QStringLiteral("failed"), !tab.reconnectPending && tab.kind == TerminalTabKind::Ssh
                                       && tab.sshPhase == ssh::SshConnectionPhase::Failed
                                       && tab.sshFailure != ssh::SshFailureKind::RemoteClosed},
        {QStringLiteral("remoteClosed"), !tab.reconnectPending && tab.kind == TerminalTabKind::Ssh
                                             && tab.sshPhase == ssh::SshConnectionPhase::Failed
                                             && tab.sshFailure == ssh::SshFailureKind::RemoteClosed},
        {QStringLiteral("workbenchOpen"), tab.workbenchOpen},
        {QStringLiteral("workbenchPage"), tab.workbenchPage},
        {QStringLiteral("workbenchSide"), tab.workbenchSide},
        {QStringLiteral("workbenchWidth"), tab.workbenchWidth},
        {QStringLiteral("composerOpen"), tab.composerOpen},
        {QStringLiteral("composerHeight"), tab.composerHeight},
        {QStringLiteral("keywordHighlightEnabled"), tab.keywordHighlightEnabled},
        {QStringLiteral("keywordHighlightRules"), keywordRulesVariant(tab)},
        {QStringLiteral("terminalEncoding"), tab.terminalEncoding},
        {QStringLiteral("sessionFontFamily"), tab.sessionFontFamily},
        {QStringLiteral("sessionFontSize"), tab.sessionFontSize},
        {QStringLiteral("sessionLigatures"), tab.sessionLigatures},
        {QStringLiteral("sessionBackgroundOpacity"), tab.sessionBackgroundOpacity},
        {QStringLiteral("sessionCursor"), tab.sessionCursor},
        {QStringLiteral("sessionForeground"), tab.sessionForeground},
        {QStringLiteral("sessionBackground"), tab.sessionBackground},
        {QStringLiteral("scriptRecordingState"), scriptRecorderStateToken(tab.scriptRecorder.state())},
        {QStringLiteral("scriptRecordingSteps"), recordedScriptStepsVariant(tab)},
        {QStringLiteral("scriptRecordingStartedUtcMs"), tab.recordingStartedUtcMs},
        {QStringLiteral("scriptPlaybackActive"), tab.scriptPlaybackActive},
        {QStringLiteral("scriptExecutionState"), scriptExecutionStateToken(execution.state)},
        {QStringLiteral("scriptExecutionId"), utf8QString(execution.scriptId)},
        {QStringLiteral("scriptExecutionName"), utf8QString(execution.scriptName)},
        {QStringLiteral("scriptExecutionDispatchedSteps"), static_cast<qulonglong>(execution.dispatchedSteps)},
        {QStringLiteral("scriptExecutionTotalSteps"), static_cast<qulonglong>(execution.totalSteps)},
    };
}

QVariantList AppController::quickCommands() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_scripts.size()));
    for (const workbench::ScriptDefinition &script : m_scripts)
    {
        QVariantList variables;
        variables.reserve(static_cast<qsizetype>(script.variables.size()));
        for (const workbench::ScriptVariable &variable : script.variables)
        {
            QStringList choices;
            choices.reserve(static_cast<qsizetype>(variable.choices.size()));
            for (const std::string &choice : variable.choices)
            {
                choices.append(utf8QString(choice));
            }
            variables.append(QVariantMap{{QStringLiteral("name"), utf8QString(variable.name)},
                                         {QStringLiteral("label"), utf8QString(variable.label)},
                                         {QStringLiteral("type"), scriptVariableTypeToken(variable.type)},
                                         {QStringLiteral("defaultValue"), utf8QString(variable.defaultValue)},
                                         {QStringLiteral("choices"), choices},
                                         {QStringLiteral("required"), variable.required}});
        }
        QVariantList steps;
        QStringList commandText;
        steps.reserve(static_cast<qsizetype>(script.steps.size()));
        commandText.reserve(static_cast<qsizetype>(script.steps.size()));
        for (const workbench::ScriptStep &step : script.steps)
        {
            const QString command = utf8QString(step.command);
            commandText.append(command);
            steps.append(QVariantMap{{QStringLiteral("command"), command},
                                     {QStringLiteral("continuation"), scriptContinuationToken(step.continuation)},
                                     {QStringLiteral("outputMarker"), utf8QString(step.outputMarker)},
                                     {QStringLiteral("timeoutMs"), step.timeoutMs}});
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(script.id)},
            {QStringLiteral("name"), utf8QString(script.name)},
            {QStringLiteral("command"), commandText.join(QLatin1Char('\n'))},
            {QStringLiteral("description"), utf8QString(script.description)},
            {QStringLiteral("shell"), quickCommandShellScopeToken(script.shellScope)},
            {QStringLiteral("variables"), variables},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("createdUtcMs"), script.createdUtcMs},
            {QStringLiteral("modifiedUtcMs"), script.modifiedUtcMs},
        });
    }
    return result;
}

QString AppController::quickCommandOperationError() const
{
    return m_quickCommandOperationError;
}

QVariantList AppController::notes() const
{
    return m_notes;
}

QVariantList AppController::noteSearchResults() const
{
    return m_noteSearchResults;
}

QString AppController::activeNotePath() const
{
    return m_activeNotePath;
}

QString AppController::activeNoteContent() const
{
    return m_activeNoteContent;
}

bool AppController::activeNoteDirty() const noexcept
{
    return m_activeNoteDirty;
}

QString AppController::noteSearchState() const
{
    return m_noteSearchState;
}

QString AppController::noteOperationError() const
{
    return m_noteOperationError;
}

QVariantList AppController::terminalHistory() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return {};
    }

    QVariantList result;
    result.reserve(
        static_cast<qsizetype>(std::min(maximumHistoryEntries, tab->capturedHistory.size() + tab->history.size())));
    QSet<QString> seen;
    const auto appendEntries = [&result, &seen](const std::vector<workbench::ShellHistoryEntry> &entries) {
        for (const workbench::ShellHistoryEntry &entry : entries)
        {
            const QString command = utf8QString(entry.command);
            if (seen.contains(command))
            {
                continue;
            }
            seen.insert(command);
            result.append(terminalHistoryValue(entry));
            if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
            {
                break;
            }
        }
    };
    appendEntries(tab->capturedHistory);
    if (result.size() < static_cast<qsizetype>(maximumHistoryEntries))
    {
        appendEntries(tab->history);
    }
    return result;
}

QVariantList AppController::terminalGlobalHistory() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(maximumHistoryEntries));
    QSet<QString> seen;

    const auto appendTab = [&result, &seen](const TerminalTab &tab) {
        const QString sourceId = !tab.sourceProfileId.isEmpty() ? tab.sourceProfileId : tab.id;
        const QString sourceLabel = !tab.title.isEmpty() ? tab.title : tab.identity;
        const auto appendEntries = [&](const std::vector<workbench::ShellHistoryEntry> &entries) {
            for (const workbench::ShellHistoryEntry &entry : entries)
            {
                const QString command = utf8QString(entry.command);
                const QString key = sourceId + QChar{u'\0'} + command;
                if (seen.contains(key))
                {
                    continue;
                }
                seen.insert(key);
                result.append(terminalHistoryValue(entry, sourceLabel, sourceId));
                if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
                {
                    break;
                }
            }
        };
        appendEntries(tab.capturedHistory);
        if (result.size() < static_cast<qsizetype>(maximumHistoryEntries))
        {
            appendEntries(tab.history);
        }
    };

    const TerminalTab *active = activeTab();
    if (active != nullptr)
    {
        appendTab(*active);
    }
    for (const std::unique_ptr<TerminalTab> &tab : m_tabs)
    {
        if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
        {
            break;
        }
        if (tab && tab.get() != active)
        {
            appendTab(*tab);
        }
    }
    return result;
}

QVariantList AppController::actions() const
{
    return m_actionRegistry.actions(activeTab() != nullptr);
}

QString AppController::terminalHistoryState() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("idle") : tab->historyState;
}

QString AppController::terminalHistoryError() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->historyError;
}

QObject *AppController::activeSftpDirectoryModel() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? nullptr : tab->sftpModel.get();
}

QString AppController::activeSftpPath() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("/") : tab->sftpPath;
}

QString AppController::activeSftpHomePath() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->sftpHomePath;
}

QVariantList AppController::recentSftpPaths() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return {};
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    if (state == nullptr)
    {
        return {};
    }
    QVariantList paths;
    paths.reserve(static_cast<qsizetype>(state->recentRemotePaths.size()));
    for (const std::string &path : state->recentRemotePaths)
    {
        paths.push_back(utf8QString(path));
    }
    return paths;
}

QVariantList AppController::bookmarkedSftpPaths() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return {};
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    if (state == nullptr)
    {
        return {};
    }
    QVariantList paths;
    paths.reserve(static_cast<qsizetype>(state->bookmarkedRemotePaths.size()));
    for (const std::string &path : state->bookmarkedRemotePaths)
    {
        paths.push_back(utf8QString(path));
    }
    return paths;
}

bool AppController::activeSftpPathBookmarked() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return false;
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    return state != nullptr && workbench::remotePathBookmarked(*state, utf8String(tab->sftpPath));
}

QString AppController::activeSftpState() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("idle") : tab->sftpState;
}

QString AppController::activeSftpError() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->sftpError;
}

QString AppController::activeSftpViewMode() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("list") : tab->sftpViewMode;
}

QString AppController::activeSftpSortColumn() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("name") : tab->sftpSortColumn;
}

bool AppController::activeSftpSortAscending() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpSortAscending;
}

bool AppController::activeSftpDirectoriesFirst() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpDirectoriesFirst;
}

bool AppController::activeSftpShowModifiedColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpShowModifiedColumn;
}

bool AppController::activeSftpShowSizeColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpShowSizeColumn;
}

bool AppController::activeSftpShowTypeColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->sftpShowTypeColumn;
}

QString AppController::activeSftpFilenameEncoding() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("utf-8") : tab->sftpFilenameEncoding;
}

bool AppController::activeSftpFollowTerminalDirectory() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->followTerminalDirectory;
}

QString AppController::activeTerminalWorkingDirectory() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->terminalWorkingDirectory;
}

QVariantList AppController::transferTasks() const
{
    return m_transferTasks;
}

QVariantList AppController::transferBatches() const
{
    return m_transferBatches;
}

int AppController::activeTransferCount() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_transferTasks, [](const QVariant &value) {
        const QString status = value.toMap().value(QStringLiteral("status")).toString();
        return status == QLatin1StringView("queued") || status == QLatin1StringView("running")
               || status == QLatin1StringView("pausing") || status == QLatin1StringView("paused")
               || status == QLatin1StringView("cancelling") || status == QLatin1StringView("needs-attention");
    }));
}

QString AppController::activeTerminalTabId() const
{
    return m_activeTabId;
}

QVariantMap AppController::activeTerminalWorkspace() const
{
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (workspace == nullptr)
    {
        return {};
    }
    return {
        {QStringLiteral("id"), utf8QString(workspace->id)},
        {QStringLiteral("title"), utf8QString(workspace->title)},
        {QStringLiteral("activePaneId"), utf8QString(workspace->activePaneId)},
        {QStringLiteral("paneCount"), static_cast<int>(workspace->restoreIntents.size())},
        {QStringLiteral("root"), terminalLayoutNodeValue(*workspace, workspace->rootNodeId)},
    };
}

QVariantMap AppController::terminalLayoutNodeValue(const workbench::TerminalWorkspaceLayout &workspace,
                                                   const std::string_view nodeId) const
{
    const auto position = std::ranges::find(workspace.nodes, nodeId, &workbench::TerminalLayoutNode::id);
    if (position == workspace.nodes.end())
    {
        return {};
    }
    if (position->kind == workbench::TerminalLayoutNodeKind::Leaf)
    {
        const TerminalTab *tab = findTabForPane(utf8QString(position->id));
        if (tab == nullptr)
        {
            return {};
        }
        return {
            {QStringLiteral("id"), utf8QString(position->id)},
            {QStringLiteral("kind"), QStringLiteral("leaf")},
            {QStringLiteral("active"), position->id == workspace.activePaneId},
            {QStringLiteral("tab"), terminalTabValue(*tab, utf8QString(workspace.id))},
        };
    }
    return {
        {QStringLiteral("id"), utf8QString(position->id)},
        {QStringLiteral("kind"), QStringLiteral("split")},
        {QStringLiteral("orientation"), position->orientation == workbench::TerminalSplitOrientation::Horizontal
                                            ? QStringLiteral("horizontal")
                                            : QStringLiteral("vertical")},
        {QStringLiteral("ratio"), position->ratio},
        {QStringLiteral("first"), terminalLayoutNodeValue(workspace, position->firstChildId)},
        {QStringLiteral("second"), terminalLayoutNodeValue(workspace, position->secondChildId)},
    };
}

QVariantMap AppController::activeRemoteTelemetry() const
{
    const TerminalTab *tab = activeTab();
    QVariantMap result{{QStringLiteral("state"), tab ? tab->telemetryState : QStringLiteral("paused")},
                       {QStringLiteral("available"), tab != nullptr && tab->telemetrySample.has_value()}};
    if (tab == nullptr || !tab->telemetrySample)
    {
        return result;
    }

    const telemetry::Sample &sample = *tab->telemetrySample;
    result.insert(QStringLiteral("os"), QString::fromStdString(sample.osName));
    result.insert(QStringLiteral("cpuPercent"), sample.cpuPercent.value_or(-1.0));
    result.insert(QStringLiteral("cpuCores"), static_cast<int>(sample.cpuCoreCount));
    result.insert(QStringLiteral("memoryUsedKiB"), QVariant::fromValue<qulonglong>(sample.memoryUsedKiB));
    result.insert(QStringLiteral("memoryTotalKiB"), QVariant::fromValue<qulonglong>(sample.memory.totalKiB));
    result.insert(QStringLiteral("memoryAvailableKiB"), QVariant::fromValue<qulonglong>(sample.memory.availableKiB));
    result.insert(QStringLiteral("memoryBuffersKiB"), QVariant::fromValue<qulonglong>(sample.memory.buffersKiB));
    result.insert(QStringLiteral("memoryCachedKiB"),
                  QVariant::fromValue<qulonglong>(sample.memory.cachedKiB + sample.memory.reclaimableKiB));
    result.insert(QStringLiteral("swapUsedKiB"),
                  QVariant::fromValue<qulonglong>(sample.memory.swapTotalKiB - sample.memory.swapFreeKiB));
    result.insert(QStringLiteral("swapTotalKiB"), QVariant::fromValue<qulonglong>(sample.memory.swapTotalKiB));
    result.insert(QStringLiteral("receivedBytesPerSecond"),
                  QVariant::fromValue<qulonglong>(sample.receivedBytesPerSecond));
    result.insert(QStringLiteral("transmittedBytesPerSecond"),
                  QVariant::fromValue<qulonglong>(sample.transmittedBytesPerSecond));
    result.insert(QStringLiteral("latencyMs"), static_cast<int>(sample.sshProbeLatencyMs));

    QVariantList cores;
    cores.reserve(static_cast<qsizetype>(sample.corePercents.size()));
    for (const double value : sample.corePercents)
    {
        cores.append(value);
    }
    result.insert(QStringLiteral("cores"), cores);

    QVariantList disks;
    disks.reserve(static_cast<qsizetype>(sample.disks.size()));
    for (const telemetry::DiskCounters &disk : sample.disks)
    {
        disks.append(QVariantMap{{QStringLiteral("mountPoint"), QString::fromStdString(disk.mountPoint)},
                                 {QStringLiteral("usedKiB"), QVariant::fromValue<qulonglong>(disk.usedKiB)},
                                 {QStringLiteral("totalKiB"), QVariant::fromValue<qulonglong>(disk.totalKiB)},
                                 {QStringLiteral("percent"), disk.usedPercent}});
    }
    result.insert(QStringLiteral("disks"), disks);

    QVariantList interfaces;
    interfaces.reserve(static_cast<qsizetype>(sample.interfaces.size()));
    for (const telemetry::NetworkRate &interface : sample.interfaces)
    {
        interfaces.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(interface.name)},
                                      {QStringLiteral("receivedBytesPerSecond"),
                                       QVariant::fromValue<qulonglong>(interface.receivedBytesPerSecond)},
                                      {QStringLiteral("transmittedBytesPerSecond"),
                                       QVariant::fromValue<qulonglong>(interface.transmittedBytesPerSecond)}});
    }
    result.insert(QStringLiteral("interfaces"), interfaces);

    QVariantList processes;
    processes.reserve(static_cast<qsizetype>(sample.processes.size()));
    for (const telemetry::ProcessMemory &process : sample.processes)
    {
        processes.append(QVariantMap{{QStringLiteral("pid"), process.pid},
                                     {QStringLiteral("memoryPercent"), process.memoryPercent},
                                     {QStringLiteral("command"), QString::fromStdString(process.command)}});
    }
    result.insert(QStringLiteral("processes"), processes);

    QVariantList history;
    history.reserve(static_cast<qsizetype>(tab->telemetryHistory.size()));
    for (const telemetry::Sample &entry : tab->telemetryHistory)
    {
        const auto rootDisk = std::ranges::find(entry.disks, std::string("/"), &telemetry::DiskCounters::mountPoint);
        history.append(QVariantMap{
            {QStringLiteral("cpu"), entry.cpuPercent.value_or(-1.0)},
            {QStringLiteral("memory"), entry.memory.totalKiB > 0 ? 100.0 * static_cast<double>(entry.memoryUsedKiB)
                                                                       / static_cast<double>(entry.memory.totalKiB)
                                                                 : 0.0},
            {QStringLiteral("disk"), rootDisk != entry.disks.end() ? rootDisk->usedPercent : 0.0},
            {QStringLiteral("received"), QVariant::fromValue<qulonglong>(entry.receivedBytesPerSecond)},
            {QStringLiteral("transmitted"), QVariant::fromValue<qulonglong>(entry.transmittedBytesPerSecond)},
            {QStringLiteral("latency"), static_cast<int>(entry.sshProbeLatencyMs)},
        });
    }
    result.insert(QStringLiteral("history"), history);
    return result;
}

QString AppController::terminalSearchQuery() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->searchQuery;
}

int AppController::terminalSearchCurrent() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchCurrent);
}

int AppController::terminalSearchTotal() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchTotal);
}

bool AppController::terminalSearchCaseSensitive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->searchCaseSensitive;
}

QString AppController::themePreference() const
{
    return config::themePreferenceToken(m_settings.theme);
}

qreal AppController::backdropOpacity() const noexcept
{
    return m_settings.backdropOpacity;
}

QString AppController::backdropPreference() const
{
    return config::backdropPreferenceToken(m_settings.backdrop);
}

QString AppController::accentPreference() const
{
    return config::accentPreferenceToken(m_settings.accent);
}

QString AppController::customAccent() const
{
    return m_settings.customAccent;
}

QString AppController::uiFontFamily() const
{
    return m_settings.uiFontFamily;
}

QString AppController::terminalFontFamily() const
{
    return m_settings.terminalFontFamily;
}

int AppController::terminalFontSize() const noexcept
{
    return m_settings.terminalFontSize;
}

bool AppController::showAllTerminalFonts() const noexcept
{
    return m_settings.showAllTerminalFonts;
}

bool AppController::terminalLigatures() const noexcept
{
    return m_settings.terminalLigatures;
}

qreal AppController::terminalBackgroundOpacity() const noexcept
{
    return m_settings.terminalBackgroundOpacity;
}

QString AppController::cursorPreference() const
{
    return config::cursorPreferenceToken(m_settings.cursor);
}

bool AppController::cursorBlink() const noexcept
{
    return m_settings.cursorBlink;
}

bool AppController::copyOnSelect() const noexcept
{
    return m_settings.copyOnSelect;
}

bool AppController::keepSelectionAfterCopy() const noexcept
{
    return m_settings.keepSelectionAfterCopy;
}

bool AppController::confirmMultilinePaste() const noexcept
{
    return m_settings.confirmMultilinePaste;
}

QString AppController::terminalRightClickBehavior() const
{
    return config::terminalRightClickPreferenceToken(m_settings.terminalRightClick);
}

QString AppController::terminalMiddleClickBehavior() const
{
    return config::terminalMiddleClickPreferenceToken(m_settings.terminalMiddleClick);
}

QString AppController::terminalWordDelimiters() const
{
    return m_settings.terminalWordDelimiters;
}

int AppController::terminalScrollRows() const noexcept
{
    return m_settings.terminalScrollRows;
}

bool AppController::sftpShowHiddenFiles() const noexcept
{
    return m_settings.sftpShowHiddenFiles;
}

bool AppController::sftpConfirmDelete() const noexcept
{
    return m_settings.sftpConfirmDelete;
}

bool AppController::closeToTray() const noexcept
{
    return m_settings.closeToTray;
}

bool AppController::performanceMode() const noexcept
{
    return m_settings.performanceMode;
}

QString AppController::languagePreference() const
{
    return config::languagePreferenceToken(m_settings.language);
}

QString AppController::aiProviderPreference() const
{
    return config::aiProviderPreferenceToken(m_settings.aiProvider);
}

QString AppController::aiReasoningPreference() const
{
    return config::aiReasoningPreferenceToken(m_settings.aiReasoning);
}

QString AppController::aiProxyPreference() const
{
    return config::aiProxyPreferenceToken(m_settings.aiProxy);
}

QString AppController::aiProxyUrl() const
{
    return m_settings.aiProxyUrl;
}

QString AppController::aiProxyUsername() const
{
    return m_settings.aiProxyUsername;
}

bool AppController::aiProxyPasswordConfigured() const
{
    const ai::AiSecretStore store(m_credentialVaults->active());
    return store.readApiKey(AiProxyCredentialReference).has_value();
}

QString AppController::aiBaseUrl() const
{
    return m_settings.aiBaseUrl;
}

QString AppController::aiEndpointPath() const
{
    return m_settings.aiEndpointPath;
}

QString AppController::aiModel() const
{
    return m_settings.aiModel;
}

QStringList AppController::aiAvailableModels() const
{
    return m_aiAvailableModels;
}

bool AppController::aiModelsLoading() const noexcept
{
    return m_aiModelsLoading;
}

QString AppController::aiModelsError() const
{
    return m_aiModelsError;
}

bool AppController::aiWebSearchAvailable() const noexcept
{
    return m_settings.aiProvider == config::AiProviderPreference::openAiResponses
           || m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt
           || m_settings.aiProvider == config::AiProviderPreference::anthropic;
}

bool AppController::aiAutomaticContext() const noexcept
{
    return m_settings.aiAutomaticContext;
}

bool AppController::aiDebugTraceEnabled() const noexcept
{
    return m_settings.aiDebugTraceEnabled;
}

QString AppController::aiDebugTracePath() const
{
    return m_aiDebugTracePath;
}

QString AppController::aiPermissionPreference() const
{
    return config::aiPermissionPreferenceToken(m_settings.aiPermission);
}

bool AppController::aiConversationHistoryEnabled() const noexcept
{
    return m_settings.aiConversationHistoryEnabled;
}

bool AppController::aiApiKeyConfigured() const
{
    const ai::AiSecretStore store(m_credentialVaults->active());
    return store.readApiKey(m_settings.aiCredentialReference.toStdString()).has_value();
}

bool AppController::aiChatGptConfigured() const
{
    const ai::AiSecretStore store(m_credentialVaults->active());
    return store.readOAuthTokens(aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString())
        .has_value();
}

QString AppController::aiChatGptAuthState() const
{
    if (m_aiSubscriptionAuth && m_aiSubscriptionAuth->active())
    {
        return m_aiChatGptDeviceUserCode.isEmpty() ? QStringLiteral("signing-in") : QStringLiteral("device-code");
    }
    if (!m_aiChatGptAuthError.isEmpty())
    {
        return QStringLiteral("error");
    }
    return aiChatGptConfigured() ? QStringLiteral("signed-in") : QStringLiteral("signed-out");
}

QString AppController::aiChatGptAccountId() const
{
    if (!m_aiChatGptAccountId.isEmpty())
    {
        return m_aiChatGptAccountId;
    }
    const ai::AiSecretStore store(m_credentialVaults->active());
    auto tokens =
        store.readOAuthTokens(aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString());
    return tokens.has_value() ? ai::OpenAiSubscriptionAuthProtocol::accountIdFromAccessToken(tokens->accessToken.view())
                              : QString{};
}

QString AppController::aiChatGptAuthError() const
{
    return m_aiChatGptAuthError;
}

QVariantMap AppController::aiChatGptDeviceCode() const
{
    if (m_aiChatGptDeviceUserCode.isEmpty() || !m_aiChatGptDeviceVerificationUrl.isValid())
    {
        return {};
    }
    return {{QStringLiteral("verificationUrl"), m_aiChatGptDeviceVerificationUrl.toString(QUrl::FullyEncoded)},
            {QStringLiteral("userCode"), m_aiChatGptDeviceUserCode}};
}

QVariantMap AppController::aiChatGptUsage() const
{
    QVariantMap value{{QStringLiteral("state"), m_aiSubscriptionUsageLoading            ? QStringLiteral("loading")
                                                : !m_aiSubscriptionUsageError.isEmpty() ? QStringLiteral("error")
                                                : m_aiSubscriptionUsage.has_value()     ? QStringLiteral("ready")
                                                                                        : QStringLiteral("idle")},
                      {QStringLiteral("error"), m_aiSubscriptionUsageError}};
    if (!m_aiSubscriptionUsage.has_value())
    {
        return value;
    }
    QVariantList limits;
    limits.reserve(static_cast<qsizetype>(m_aiSubscriptionUsage->additional.size() + 1));
    limits.push_back(subscriptionUsageLimitValue(m_aiSubscriptionUsage->codex));
    for (const auto &limit : m_aiSubscriptionUsage->additional)
    {
        limits.push_back(subscriptionUsageLimitValue(limit));
    }
    value.insert(QStringLiteral("planType"), m_aiSubscriptionUsage->planType);
    value.insert(QStringLiteral("limits"), limits);
    value.insert(QStringLiteral("hasCredits"), m_aiSubscriptionUsage->hasCredits);
    value.insert(QStringLiteral("unlimitedCredits"), m_aiSubscriptionUsage->unlimitedCredits);
    value.insert(QStringLiteral("creditBalance"), m_aiSubscriptionUsage->creditBalance);
    return value;
}

QObject *AppController::aiActivity() noexcept
{
    return &m_aiActivity;
}

QObject *AppController::aiConversationHistory() noexcept
{
    return m_aiConversationHistory.get();
}

QObject *AppController::activeAiConversation() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? nullptr : tab->aiConversation.get();
}

QString AppController::activeAiState() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("idle") : tab->aiState;
}

QString AppController::activeAiError() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->aiError;
}

QVariantMap AppController::activeAiErrorRecovery() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || (tab->aiState != QStringLiteral("error") && tab->aiState != QStringLiteral("cancelled")))
    {
        return {};
    }

    if (!tab->aiProviderErrorCode.has_value() && tab->aiState != QStringLiteral("cancelled"))
    {
        return {{QStringLiteral("code"), QStringLiteral("unknown")},
                {QStringLiteral("hint"), tr("Retry the response. If it fails again, review the provider settings.")},
                {QStringLiteral("messageAnchored"), tab->aiAssistantMessageId != 0},
                {QStringLiteral("retryAvailable"), true},
                {QStringLiteral("settingsAvailable"), false},
                {QStringLiteral("newConversationAvailable"), false}};
    }
    const ai::AiProviderErrorCode code = tab->aiProviderErrorCode.value_or(ai::AiProviderErrorCode::cancelled);
    const ai::AiProviderRecoveryPlan recovery = ai::AiProviderRecoveryPolicy::plan(code);
    QString codeToken;
    QString hint;
    switch (code)
    {
        case ai::AiProviderErrorCode::network:
            codeToken = QStringLiteral("network");
            hint = tr("Check the connection and retry this response.");
            break;
        case ai::AiProviderErrorCode::authentication:
            codeToken = QStringLiteral("authentication");
            hint = m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt
                       ? tr("Sign in with ChatGPT again in AI settings, then retry this response.")
                       : tr("Update the API key in AI settings, then retry this response.");
            break;
        case ai::AiProviderErrorCode::rateLimited:
            codeToken = QStringLiteral("rate_limited");
            hint = tab->aiProviderRetryAfterMilliseconds.has_value()
                       ? tr("The provider asked ztermy to wait about %1 seconds before retrying.")
                             .arg((*tab->aiProviderRetryAfterMilliseconds / 1000)
                                  + (*tab->aiProviderRetryAfterMilliseconds % 1000 != 0 ? 1 : 0))
                       : tr("Wait a moment and retry, or switch the provider or model in AI settings.");
            break;
        case ai::AiProviderErrorCode::quotaExceeded:
            codeToken = QStringLiteral("quota_exceeded");
            hint = tr("Switch the provider or model, or update the provider quota before retrying.");
            break;
        case ai::AiProviderErrorCode::invalidRequest:
            codeToken = QStringLiteral("invalid_request");
            hint = tr("Check the endpoint, model, and provider settings before retrying.");
            break;
        case ai::AiProviderErrorCode::server:
            codeToken = QStringLiteral("server");
            hint = tr("The provider is temporarily unavailable. Retry in a moment.");
            break;
        case ai::AiProviderErrorCode::cancelled:
            codeToken = QStringLiteral("cancelled");
            hint = tr("The response was stopped. Retry when ready.");
            break;
        case ai::AiProviderErrorCode::protocol:
            codeToken = QStringLiteral("protocol");
            hint = tr("Check endpoint compatibility in AI settings, or retry the response.");
            break;
        case ai::AiProviderErrorCode::contextOverflow:
            codeToken = QStringLiteral("context_overflow");
            hint = tr("Start a new conversation or send a shorter request with less attached context.");
            break;
    }
    return {{QStringLiteral("code"), codeToken},
            {QStringLiteral("hint"), hint},
            {QStringLiteral("messageAnchored"), tab->aiAssistantMessageId != 0},
            {QStringLiteral("retryAfterMilliseconds"),
             QVariant::fromValue<qulonglong>(tab->aiProviderRetryAfterMilliseconds.value_or(0))},
            {QStringLiteral("retryAvailable"), recovery.retryAvailable},
            {QStringLiteral("settingsAvailable"), recovery.settingsAvailable},
            {QStringLiteral("newConversationAvailable"), recovery.newConversationAvailable}};
}

QString AppController::activeAiContextPreview() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->aiContextPreview;
}

QVariantList AppController::activeAiContextItems() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QVariantList{} : tab->aiContextItems;
}

QVariantMap AppController::activeAiCompaction() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QVariantMap{} : tab->aiCompaction;
}

QVariantMap AppController::aiPrivacyDiagnostics() const
{
    ai::AiPrivacySnapshot snapshot{
        .providerKind = aiProviderPreference(),
        .endpoint = QUrl(aiBaseUrl()),
        .permissionMode = aiPermissionPreference(),
        .modelConfigured = !aiModel().trimmed().isEmpty(),
        .apiKeyConfigured = aiApiKeyConfigured(),
        .automaticContext = aiAutomaticContext(),
        .encryptedHistoryEnabled = aiConversationHistoryEnabled(),
    };
    for (const auto &tab : m_tabs)
    {
        if (!tab->aiConversationId.isEmpty())
        {
            ++snapshot.activeConversations;
        }
        if (aiTurnActive(*tab))
        {
            ++snapshot.activeRequests;
        }
    }
    if (const TerminalTab *tab = activeTab(); tab != nullptr)
    {
        snapshot.contextItems = static_cast<std::size_t>(tab->aiContextItems.size());
        snapshot.serializedContextBytes = static_cast<std::size_t>(tab->aiContextPreview.toUtf8().size());
        for (const QVariant &itemValue : tab->aiContextItems)
        {
            const QVariantMap item = itemValue.toMap();
            snapshot.redactedContextItems += item.value(QStringLiteral("redacted")).toBool() ? 1U : 0U;
            snapshot.truncatedContextItems += item.value(QStringLiteral("truncated")).toBool() ? 1U : 0U;
        }
    }
    const auto servers = m_mcpRuntime.servers();
    snapshot.mcpServers = servers.size();
    snapshot.readyMcpServers =
        static_cast<std::size_t>(std::ranges::count_if(servers, [](const ai::McpServerSnapshot &server) {
            return server.state == QStringLiteral("ready");
        }));
    const auto tools = m_mcpRuntime.tools();
    snapshot.approvedMcpTools =
        static_cast<std::size_t>(std::ranges::count_if(tools, [](const ai::McpToolSnapshot &tool) {
            return tool.approved;
        }));
    return ai::buildAiPrivacyDiagnostics(snapshot).toVariantMap();
}

QVariantList AppController::aiPermissionRules() const
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(m_aiActionToolDispatcher.permissionRules().size()));
    for (const auto &rule : m_aiActionToolDispatcher.permissionRules())
    {
        QString profileName;
        if (!rule.profileId.empty())
        {
            const auto profile = std::ranges::find_if(m_profiles, [&rule](const ssh::SshProfile &candidate) {
                return candidate.id == rule.profileId;
            });
            if (profile != m_profiles.end())
            {
                profileName = utf8QString(profile->name);
            }
        }
        values.push_back(QVariantMap{
            {QStringLiteral("id"), utf8QString(rule.id)},
            {QStringLiteral("capability"), aiPermissionCapabilityToken(rule.capability)},
            {QStringLiteral("matcher"), aiPermissionMatcherToken(rule.matcher)},
            {QStringLiteral("pattern"), utf8QString(rule.pattern)},
            {QStringLiteral("decision"), aiPermissionDispositionToken(rule.disposition)},
            {QStringLiteral("duration"), aiPermissionDurationToken(rule.duration)},
            {QStringLiteral("profileId"), utf8QString(rule.profileId)},
            {QStringLiteral("profileName"), profileName},
            {QStringLiteral("enabled"), rule.enabled},
        });
    }
    return values;
}

QString AppController::aiPermissionRuleError() const
{
    return m_aiPermissionRuleError;
}

QVariantList AppController::aiQuickMessages() const
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(m_aiQuickMessages.size()));
    for (const ai::AiQuickMessage &message : m_aiQuickMessages)
    {
        values.push_back(QVariantMap{{QStringLiteral("id"), utf8QString(message.id)},
                                     {QStringLiteral("name"), utf8QString(message.name)},
                                     {QStringLiteral("slug"), utf8QString(message.slug)},
                                     {QStringLiteral("content"), utf8QString(message.content)},
                                     {QStringLiteral("description"), utf8QString(message.description)}});
    }
    return values;
}

QString AppController::aiQuickMessageError() const
{
    return m_aiQuickMessageError;
}

QVariantList AppController::aiUserSkills() const
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(m_aiUserSkills.size()));
    for (const ai::AiUserSkill &skill : m_aiUserSkills)
    {
        QStringList warnings;
        warnings.reserve(static_cast<qsizetype>(skill.warnings.size()));
        for (const ai::AiUserSkillWarning warning : skill.warnings)
        {
            warnings.push_back(aiUserSkillWarningText(warning));
        }
        values.push_back(QVariantMap{{QStringLiteral("id"), utf8QString(skill.id)},
                                     {QStringLiteral("name"), utf8QString(skill.name)},
                                     {QStringLiteral("description"), utf8QString(skill.description)},
                                     {QStringLiteral("ready"), skill.ready},
                                     {QStringLiteral("warnings"), warnings}});
    }
    return values;
}

QString AppController::aiUserSkillsPath() const
{
    return QDir::toNativeSeparators(m_aiUserSkillCatalog.rootPath());
}

QString AppController::aiUserSkillsState() const
{
    return m_aiUserSkillsState;
}

QString AppController::aiUserSkillsError() const
{
    return m_aiUserSkillsError;
}

QVariantMap AppController::activeAiToolApproval() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return {};
    }
    if (tab->pendingAiMcpCall.has_value())
    {
        const auto &pending = *tab->pendingAiMcpCall;
        const QString arguments = utf8QString(pending.call.argumentsJson);
        return {{QStringLiteral("visible"), true},
                {QStringLiteral("toolCallId"), utf8QString(pending.call.id)},
                {QStringLiteral("command"),
                 tr("MCP tool: %1\nServer: %2\n\nArguments (untrusted):\n%3")
                     .arg(utf8QString(pending.tool.exposedName), utf8QString(pending.tool.serverNamespace),
                          arguments.left(qsizetype{16} * 1024))},
                {QStringLiteral("kind"), QStringLiteral("mcp_tool")},
                {QStringLiteral("sessionId"), tab->id},
                {QStringLiteral("sessionGeneration"), QVariant::fromValue<qulonglong>(tab->reconnectGeneration)},
                {QStringLiteral("ruleSupported"), true},
                {QStringLiteral("ruleSubject"), utf8QString(pending.call.name)},
                {QStringLiteral("ruleCapability"), QStringLiteral("mcp-tool")},
                {QStringLiteral("ruleDefaultMatcher"), QStringLiteral("exact")},
                {QStringLiteral("ruleDefaultPattern"), utf8QString(pending.call.name)},
                {QStringLiteral("profileAvailable"), !tab->sourceProfileId.isEmpty()},
                {QStringLiteral("highRisk"), true},
                {QStringLiteral("riskReason"),
                 tr("MCP tools run in an external process and their descriptions and results are untrusted.")}};
    }
    if (!tab->pendingAiAction.has_value())
    {
        return {};
    }
    const auto &action = *tab->pendingAiAction;
    QString actionText;
    QString actionKind;
    switch (action.kind)
    {
        case ai::AiTerminalActionKind::runCommand:
            actionText = utf8QString(action.command);
            actionKind = QStringLiteral("run_command");
            break;
        case ai::AiTerminalActionKind::writeToPty:
            actionText = utf8QString(action.ptyData);
            if (action.appendEnter)
            {
                actionText += QStringLiteral("\n↵ Enter");
            }
            actionKind = QStringLiteral("write_to_pty");
            break;
        case ai::AiTerminalActionKind::interruptCommand:
            actionText = tr("Send Ctrl+C to tracked command %1").arg(utf8QString(action.commandId));
            actionKind = QStringLiteral("interrupt_command");
            break;
        case ai::AiTerminalActionKind::saveRunbook:
        {
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(action.payloadJson));
            const QJsonObject runbook = document.object();
            actionText = tr("Save runbook: %1").arg(runbook.value(QStringLiteral("name")).toString());
            const QString description = runbook.value(QStringLiteral("description")).toString().trimmed();
            if (!description.isEmpty())
            {
                actionText += QStringLiteral("\n\n") + description;
            }
            const QJsonArray steps = runbook.value(QStringLiteral("steps")).toArray();
            for (qsizetype index = 0; index < steps.size(); ++index)
            {
                actionText += QStringLiteral("\n\n%1. %2")
                                  .arg(index + 1)
                                  .arg(steps.at(index).toObject().value(QStringLiteral("command")).toString());
            }
            actionKind = QStringLiteral("save_runbook");
            break;
        }
        case ai::AiTerminalActionKind::enqueueSftpDownload:
        case ai::AiTerminalActionKind::enqueueSftpUpload:
        {
            const QJsonObject transfer =
                QJsonDocument::fromJson(QByteArray::fromStdString(action.payloadJson)).object();
            const bool upload = action.kind == ai::AiTerminalActionKind::enqueueSftpUpload;
            actionText = upload ? tr("Upload with SFTP") : tr("Download with SFTP");
            actionText += QStringLiteral("\n\n%1\n→\n%2")
                              .arg(upload ? transfer.value(QStringLiteral("local_path")).toString()
                                          : transfer.value(QStringLiteral("remote_path")).toString(),
                                   upload ? transfer.value(QStringLiteral("remote_path")).toString()
                                          : transfer.value(QStringLiteral("local_path")).toString());
            actionKind = upload ? QStringLiteral("queue_sftp_upload") : QStringLiteral("queue_sftp_download");
            break;
        }
    }
    QString ruleDefaultMatcher = QStringLiteral("exact");
    QString ruleDefaultPattern = utf8QString(action.permissionSubject);
    if (action.kind == ai::AiTerminalActionKind::runCommand)
    {
        const auto suggestion = ai::AiPermissionPolicy::suggestCommandRule(action.command);
        ruleDefaultMatcher = aiPermissionMatcherToken(suggestion.matcher);
        ruleDefaultPattern = utf8QString(suggestion.pattern);
    }
    else if (action.permissionCapability == ai::AiPermissionCapability::ptyInput)
    {
        ruleDefaultMatcher = QStringLiteral("all");
        ruleDefaultPattern.clear();
    }
    return {{QStringLiteral("visible"), true},
            {QStringLiteral("toolCallId"), utf8QString(action.dispatchKey.toolCallId)},
            {QStringLiteral("command"), actionText},
            {QStringLiteral("kind"), actionKind},
            {QStringLiteral("sessionId"), utf8QString(action.target.sessionId)},
            {QStringLiteral("sessionGeneration"), QVariant::fromValue<qulonglong>(action.target.sessionGeneration)},
            {QStringLiteral("ruleSupported"), true},
            {QStringLiteral("ruleSubject"), utf8QString(action.permissionSubject)},
            {QStringLiteral("ruleCapability"), aiPermissionCapabilityToken(action.permissionCapability)},
            {QStringLiteral("ruleDefaultMatcher"), ruleDefaultMatcher},
            {QStringLiteral("ruleDefaultPattern"), ruleDefaultPattern},
            {QStringLiteral("profileAvailable"), !tab->sourceProfileId.isEmpty()},
            {QStringLiteral("highRisk"), action.risk.highRisk()},
            {QStringLiteral("riskReason"), utf8QString(action.risk.reason)}};
}

QVariantList AppController::mcpServers() const
{
    QVariantList values;
    for (const auto &snapshot : m_mcpRuntime.servers())
    {
        QString trust = QStringLiteral("disabled");
        if (snapshot.record.configuration.identity.trust == ai::McpServerTrust::observe)
        {
            trust = QStringLiteral("observe");
        }
        else if (snapshot.record.configuration.identity.trust == ai::McpServerTrust::execute)
        {
            trust = QStringLiteral("execute");
        }
        values.push_back(QVariantMap{
            {QStringLiteral("id"), utf8QString(snapshot.record.configuration.identity.id)},
            {QStringLiteral("namespace"), utf8QString(snapshot.record.configuration.identity.nameSpace)},
            {QStringLiteral("program"), snapshot.record.configuration.program},
            {QStringLiteral("arguments"), snapshot.record.configuration.arguments},
            {QStringLiteral("workingDirectory"), snapshot.record.configuration.workingDirectory},
            {QStringLiteral("trust"), trust},
            {QStringLiteral("enabled"), snapshot.record.enabled},
            {QStringLiteral("state"), snapshot.state},
            {QStringLiteral("error"), snapshot.error},
        });
    }
    return values;
}

QVariantList AppController::mcpTools() const
{
    QVariantList values;
    for (const auto &snapshot : m_mcpRuntime.tools())
    {
        values.push_back(QVariantMap{{QStringLiteral("serverId"), utf8QString(snapshot.tool.serverId)},
                                     {QStringLiteral("namespace"), utf8QString(snapshot.tool.serverNamespace)},
                                     {QStringLiteral("remoteName"), utf8QString(snapshot.tool.remoteName)},
                                     {QStringLiteral("exposedName"), utf8QString(snapshot.tool.exposedName)},
                                     {QStringLiteral("description"), utf8QString(snapshot.tool.description)},
                                     {QStringLiteral("inputSchema"), utf8QString(snapshot.tool.inputSchemaJson)},
                                     {QStringLiteral("schemaDigest"), utf8QString(snapshot.tool.schemaDigest)},
                                     {QStringLiteral("approved"), snapshot.approved}});
    }
    return values;
}

QString AppController::mcpOperationError() const
{
    return m_mcpRuntime.operationError();
}

void AppController::initializeAiPrivacySignals()
{
    QObject::connect(this, &AppController::applicationSettingsChanged, this,
                     &AppController::aiPrivacyDiagnosticsChanged);
    QObject::connect(this, &AppController::credentialVaultChanged, this, &AppController::aiPrivacyDiagnosticsChanged);
    QObject::connect(this, &AppController::aiConversationChanged, this, &AppController::aiPrivacyDiagnosticsChanged);
    QObject::connect(this, &AppController::mcpConfigurationChanged, this, &AppController::aiPrivacyDiagnosticsChanged);
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::aiPrivacyDiagnosticsChanged);
}

void AppController::initializeAiDebugTrace()
{
    m_aiProviderClient.setTraceHandler([this](const ai::ProviderHttpClient::RequestId requestId, const QString &event,
                                              const QByteArray &bytes) {
        QJsonObject payload{{QStringLiteral("request_id"), static_cast<qint64>(requestId)}};
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && (document.isObject() || document.isArray()))
        {
            const QJsonValue data = document.isObject() ? QJsonValue{document.object()} : QJsonValue{document.array()};
            payload.insert(QStringLiteral("data"), ai::sanitizeAiTraceValue(data));
        }
        else
        {
            payload.insert(QStringLiteral("text"), QString::fromUtf8(bytes));
            payload.insert(QStringLiteral("base64"), QString::fromLatin1(bytes.toBase64()));
        }
        appendAiDebugTrace(event, payload);
    });
    configureAiDebugTrace();
}

void AppController::configureAiDebugTrace()
{
    if (m_aiDebugTrace)
    {
        m_aiDebugTrace->stop();
        m_aiDebugTrace.reset();
    }
    m_aiDebugTracePath.clear();
    if (!m_settings.aiDebugTraceEnabled)
    {
        return;
    }

    QDir directory(QFileInfo(m_settingsStore.filePath()).absolutePath());
    if (!directory.mkpath(QStringLiteral("ai-debug")) || !directory.cd(QStringLiteral("ai-debug")))
    {
        qCWarning(appControllerLog) << "Could not create the AI debug trace directory";
        return;
    }
    m_aiDebugTracePath =
        directory.filePath(QStringLiteral("ai-trace-%1.jsonl")
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    m_aiDebugTrace = std::make_shared<logging::SessionLogWriter>();
    if (!m_aiDebugTrace->start(m_aiDebugTracePath))
    {
        m_aiDebugTrace.reset();
        m_aiDebugTracePath.clear();
    }
}

void AppController::appendAiDebugTrace(const QString &event, const QJsonObject &payload)
{
    if (!m_settings.aiDebugTraceEnabled || !m_aiDebugTrace)
    {
        return;
    }
    QJsonObject record{{QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("event"), event},
                       {QStringLiteral("payload"), payload}};
    QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_aiDebugTrace->append(std::as_bytes(std::span(line.constData(), static_cast<std::size_t>(line.size()))));
}

void AppController::retranslateUiState()
{
    for (const auto &tab : m_tabs)
    {
        if (tab->kind == TerminalTabKind::Local)
        {
            tab->status = tab->running ? QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local PowerShell connected")
                                       : QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local terminal stopped");
        }
        else if (tab->reconnectPending)
        {
            tab->status = QCoreApplication::translate("AppController", "Waiting to reconnect to the SSH host...");
        }
        else
        {
            tab->status = localizedSshStatus(tab->sshPhase, tab->sshFailure);
        }
    }
    setCredentialOperationError({});
    showActiveTab();
    emit terminalTabsChanged();
    emit actionRegistryChanged();
}

QString AppController::credentialStoragePreference() const
{
    return config::credentialStoragePreferenceToken(m_settings.credentialStorage);
}

QString AppController::effectiveCredentialStorage() const
{
    return QString::fromLatin1(security::credentialStorageToken(m_credentialVaults->storage()));
}

bool AppController::portableVaultInitialized() const noexcept
{
    return m_credentialVaults->portableInitialized();
}

bool AppController::portableVaultLocked() const noexcept
{
    return m_credentialVaults->portableLocked();
}

QString AppController::credentialOperationError() const
{
    return m_credentialOperationError;
}

QVariantList AppController::portForwardingRules() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_portForwardingRules.size()));
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        const auto profile = std::ranges::find(m_profiles, rule.profileId, &ssh::SshProfile::id);
        const PortForwardingRuntime *runtime = findPortForwardingRuntime(rule.id);
        const forwarding::PortForwardingJobSnapshot snapshot =
            runtime != nullptr && runtime->job ? runtime->job->snapshot() : forwarding::PortForwardingJobSnapshot{};
        const bool waitingForPortableVault = rule.autoStart && runtime == nullptr
                                             && snapshot.state == forwarding::PortForwardingJobState::Stopped
                                             && portableVaultLocked();
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(rule.id)},
            {QStringLiteral("label"), utf8QString(rule.label)},
            {QStringLiteral("profileId"), utf8QString(rule.profileId)},
            {QStringLiteral("profileName"), profile == m_profiles.end() ? QString{} : utf8QString(profile->name)},
            {QStringLiteral("type"), forwardingTypeToken(rule.type)},
            {QStringLiteral("bindHost"), utf8QString(rule.bind.host)},
            {QStringLiteral("bindPort"), rule.bind.port},
            {QStringLiteral("destinationHost"), utf8QString(rule.destination.host)},
            {QStringLiteral("destinationPort"), rule.destination.port},
            {QStringLiteral("autoStart"), rule.autoStart},
            {QStringLiteral("state"),
             waitingForPortableVault ? QStringLiteral("waiting") : forwardingStateToken(snapshot.state)},
            {QStringLiteral("failure"), forwardingFailureToken(snapshot.failure)},
            {QStringLiteral("activeClients"), static_cast<qulonglong>(snapshot.activeClients)},
            {QStringLiteral("bytesFromClients"), static_cast<qulonglong>(snapshot.bytesFromClients)},
            {QStringLiteral("bytesToClients"), static_cast<qulonglong>(snapshot.bytesToClients)},
            {QStringLiteral("rejectedClients"), static_cast<qulonglong>(snapshot.rejectedClients)},
        });
    }
    return result;
}

QString AppController::portForwardingOperationError() const
{
    return m_portForwardingOperationError;
}

QString AppController::startLocalTerminal()
{
    return startLocalTerminalAt({});
}

QString AppController::startLocalTerminalAt(const QString &workingDirectory, const QString &preferredTitle)
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return {};
    }

    const std::string previousActiveWorkspaceId = m_workspaceState.activeTerminalWorkspaceId;
    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = tab->id;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString defaultTitle = tr("PowerShell %1").arg(m_nextLocalTabNumber++);
    tab->title = preferredTitle.trimmed().isEmpty() ? defaultTitle : preferredTitle.trimmed();
    tab->status = tr("Starting local terminal...");
    tab->kind = TerminalTabKind::Local;
    tab->local = m_localSessionFactory();
    if (!tab->local)
    {
        return {};
    }
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    QString tabId = tab->id;
    auto workspace = workbench::makeSinglePaneTerminalWorkspace(
        utf8String(tab->workspaceId), utf8String(tab->paneId),
        {.id = utf8String(tab->id), .title = utf8String(tab->title), .kind = workbench::TerminalRestoreKind::Local});
    workspace.title = utf8String(tab->title);
    connectLocalTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    m_workspaceState.terminalWorkspaces.push_back(std::move(workspace));
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tabId);
    if (!persistTerminalWorkspaces())
    {
        m_workspaceState.terminalWorkspaces.pop_back();
        m_workspaceState.activeTerminalWorkspaceId = previousActiveWorkspaceId;
        m_tabs.pop_back();
        return {};
    }
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->local)
    {
        return {};
    }
    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
    if (error)
    {
        created->status = tr("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        showActiveTab();
        emit terminalTabsChanged();
    }
    else if (!workingDirectory.trimmed().isEmpty())
    {
        const QString command = QLatin1StringView("Set-Location -LiteralPath ")
                                + utf8QString(terminal::quoteShellPath(utf8String(workingDirectory.trimmed()),
                                                                       terminal::ShellDialect::PowerShell))
                                + QLatin1Char('\r');
        created->local->queuePaste(command.toUtf8());
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return tabId;
}

bool AppController::activateTerminalTab(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const TerminalTab *focused = findTabForPane(utf8QString(workspace->activePaneId));
    if (focused == nullptr)
    {
        return false;
    }
    m_focusedTabId = focused->id;
    if (m_activeTabId == workspaceId)
    {
        updateTelemetryVisibility();
        showActiveTab();
        return true;
    }
    m_activeTabId = workspaceId;
    m_workspaceState.activeTerminalWorkspaceId = utf8String(workspaceId);
    static_cast<void>(persistTerminalWorkspaces());
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::closeTerminalTab(const QString &id)
{
    return closeTerminalTabInternal(id, true);
}

void AppController::recordClosedTerminal(const QString &workspaceId)
{
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(workspaceId);
    if (workspace == nullptr)
    {
        return;
    }
    const TerminalTab *tab = findTabForPane(utf8QString(workspace->activePaneId));
    if (tab == nullptr)
    {
        const QString firstId = firstTabIdForWorkspace(*workspace);
        tab = findTab(firstId);
    }
    if (tab == nullptr || (tab->kind == TerminalTabKind::Ssh && tab->sourceProfileId.isEmpty()))
    {
        return;
    }
    m_closedTerminalTabs.push_front({.kind = tab->kind,
                                     .sourceProfileId = tab->sourceProfileId,
                                     .title = utf8QString(workspace->title),
                                     .workingDirectory = tab->terminalWorkingDirectory});
    constexpr std::size_t maximumClosedTerminalDescriptions = 10;
    if (m_closedTerminalTabs.size() > maximumClosedTerminalDescriptions)
    {
        m_closedTerminalTabs.pop_back();
    }
}

bool AppController::closeTerminalTabInternal(const QString &id, const bool recordClosed)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    const auto workspacePosition = std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(workspaceId),
                                                     &workbench::TerminalWorkspaceLayout::id);
    if (workspacePosition == m_workspaceState.terminalWorkspaces.end())
    {
        return false;
    }
    if (recordClosed && !m_shutdownStarted)
    {
        recordClosedTerminal(workspaceId);
    }
    const bool closingActive = workspaceId == m_activeTabId;
    const auto workspaceIndex =
        static_cast<std::size_t>(std::distance(m_workspaceState.terminalWorkspaces.begin(), workspacePosition));
    std::erase_if(m_tabs, [this, &workspaceId](const std::unique_ptr<TerminalTab> &tab) {
        if (tab->workspaceId != workspaceId)
        {
            return false;
        }
        if (tab->id == m_hostKeyTabId)
        {
            clearHostKeyPrompt();
        }
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
        if (tab->semanticObserver)
        {
            tab->semanticObserver->finish(terminal::CommandCompletionReason::disconnect);
        }
        if (tab->sessionLog)
        {
            tab->sessionLog->stop();
        }
        if (!tab->aiConversationId.isEmpty())
        {
            m_aiActionToolDispatcher.clearConversation(utf8String(tab->aiConversationId));
            m_aiCommandTracker.clearConversation(utf8String(tab->aiConversationId));
        }
        stopSftpSession(*tab);
        m_terminalViewports.remove(tab->paneId);
        return true;
    });
    m_workspaceState.terminalWorkspaces.erase(workspacePosition);
    emit terminalTabsChanged();

    if (closingActive)
    {
        if (m_workspaceState.terminalWorkspaces.empty())
        {
            m_activeTabId.clear();
            m_focusedTabId.clear();
            m_workspaceState.activeTerminalWorkspaceId.clear();
            emitActiveTerminalContextChanged();
        }
        else
        {
            const std::size_t nextIndex = std::min(workspaceIndex, m_workspaceState.terminalWorkspaces.size() - 1U);
            m_activeTabId.clear();
            activateTerminalTab(utf8QString(m_workspaceState.terminalWorkspaces[nextIndex].id));
        }
    }
    static_cast<void>(persistTerminalWorkspaces());
    return true;
}

bool AppController::duplicateTerminalTab(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const TerminalTab *source = findTabForPane(utf8QString(workspace->activePaneId));
    if (source == nullptr)
    {
        source = findTab(firstTabIdForWorkspace(*workspace));
    }
    if (source == nullptr)
    {
        return false;
    }
    if (source->kind == TerminalTabKind::Local)
    {
        return !startLocalTerminalAt(source->terminalWorkingDirectory, utf8QString(workspace->title)).isEmpty();
    }
    if (source->sourceProfileId.isEmpty() || !connectHostProfile(source->sourceProfileId, {}))
    {
        return false;
    }
    return setTerminalTabTitle(m_activeTabId, utf8QString(workspace->title));
}

bool AppController::reopenLastClosedTerminalTab()
{
    if (m_closedTerminalTabs.empty())
    {
        return false;
    }
    const ClosedTerminalDescription description = m_closedTerminalTabs.front();
    m_closedTerminalTabs.pop_front();
    bool reopened = false;
    if (description.kind == TerminalTabKind::Local)
    {
        reopened = !startLocalTerminalAt(description.workingDirectory, description.title).isEmpty();
    }
    else if (!description.sourceProfileId.isEmpty())
    {
        reopened = connectHostProfile(description.sourceProfileId, {});
        if (reopened)
        {
            reopened = setTerminalTabTitle(m_activeTabId, description.title);
        }
    }
    if (!reopened)
    {
        m_closedTerminalTabs.push_front(description);
    }
    emit terminalTabsChanged();
    return reopened;
}

bool AppController::closeOtherTerminalTabs(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    if (findTerminalWorkspace(workspaceId) == nullptr)
    {
        return false;
    }
    std::vector<QString> toClose;
    for (auto position = m_workspaceState.terminalWorkspaces.rbegin();
         position != m_workspaceState.terminalWorkspaces.rend(); ++position)
    {
        if (position->id != utf8String(workspaceId))
        {
            toClose.push_back(utf8QString(position->id));
        }
    }
    for (const QString &candidate : toClose)
    {
        static_cast<void>(closeTerminalTab(candidate));
    }
    return !toClose.empty();
}

bool AppController::closeTerminalTabsToRight(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    const auto position = std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(workspaceId),
                                            &workbench::TerminalWorkspaceLayout::id);
    if (position == m_workspaceState.terminalWorkspaces.end())
    {
        return false;
    }
    std::vector<QString> toClose;
    for (auto candidate = m_workspaceState.terminalWorkspaces.rbegin(); candidate.base() != std::next(position);
         ++candidate)
    {
        toClose.push_back(utf8QString(candidate->id));
    }
    for (const QString &candidate : toClose)
    {
        static_cast<void>(closeTerminalTab(candidate));
    }
    return !toClose.empty();
}

bool AppController::moveTerminalTab(const QString &id, const int targetIndex)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    workbench::WorkspaceState candidate = m_workspaceState;
    auto position = std::ranges::find(candidate.terminalWorkspaces, utf8String(workspaceId),
                                      &workbench::TerminalWorkspaceLayout::id);
    if (position == candidate.terminalWorkspaces.end() || targetIndex < 0
        || static_cast<std::size_t>(targetIndex) >= candidate.terminalWorkspaces.size())
    {
        return false;
    }
    const auto currentIndex = static_cast<int>(std::distance(candidate.terminalWorkspaces.begin(), position));
    if (currentIndex == targetIndex)
    {
        return true;
    }
    workbench::TerminalWorkspaceLayout moved = std::move(*position);
    candidate.terminalWorkspaces.erase(position);
    candidate.terminalWorkspaces.insert(candidate.terminalWorkspaces.begin() + targetIndex, std::move(moved));
    if (!saveWorkspaceStateCandidate(candidate))
    {
        return false;
    }
    m_workspaceState = std::move(candidate);
    emit terminalTabsChanged();
    return true;
}

bool AppController::setTerminalTabTitle(const QString &id, const QString &title)
{
    const QString normalized = title.trimmed();
    if (normalized.isEmpty() || normalized.size() > 256)
    {
        return false;
    }
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const std::string previousWorkspaceTitle = workspace->title;
    std::vector<std::string> previousIntentTitles;
    previousIntentTitles.reserve(workspace->restoreIntents.size());
    for (const workbench::TerminalRestoreIntent &intent : workspace->restoreIntents)
    {
        previousIntentTitles.push_back(intent.title);
    }
    std::vector<QString> previousTabTitles;
    previousTabTitles.reserve(m_tabs.size());
    for (const auto &tab : m_tabs)
    {
        previousTabTitles.push_back(tab->title);
    }
    workspace->title = utf8String(normalized);
    for (workbench::TerminalRestoreIntent &intent : workspace->restoreIntents)
    {
        intent.title = utf8String(normalized);
    }
    for (const auto &tab : m_tabs)
    {
        if (tab->workspaceId == workspaceId)
        {
            tab->title = normalized;
        }
    }
    if (!persistTerminalWorkspaces())
    {
        workspace->title = previousWorkspaceTitle;
        for (std::size_t index = 0; index < workspace->restoreIntents.size(); ++index)
        {
            workspace->restoreIntents[index].title = previousIntentTitles[index];
        }
        for (std::size_t index = 0; index < m_tabs.size(); ++index)
        {
            m_tabs[index]->title = previousTabTitles[index];
        }
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

QVariantMap AppController::activeCommandBlockActions() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->semanticObserver)
    {
        return {{QStringLiteral("commandAvailable"), false}, {QStringLiteral("outputAvailable"), false}};
    }
    const terminal::SemanticTerminalSnapshot snapshot = tab->semanticObserver->snapshot();
    const auto block = std::ranges::find_if(snapshot.commandBlocks.rbegin(), snapshot.commandBlocks.rend(),
                                            [](const terminal::CommandBlock &candidate) {
                                                return !candidate.command.empty();
                                            });
    if (block == snapshot.commandBlocks.rend())
    {
        return {{QStringLiteral("commandAvailable"), false}, {QStringLiteral("outputAvailable"), false}};
    }
    return {
        {QStringLiteral("commandAvailable"), true},
        {QStringLiteral("commandApproximate"),
         block->capability != terminal::TerminalSemanticCapability::rich
             || block->boundaryConfidence != terminal::CommandBoundaryConfidence::exact},
        {QStringLiteral("outputAvailable"), block->hasCompleteOutput() && !block->retainedOutput.empty()},
        {QStringLiteral("outputPartial"), !block->hasCompleteOutput()},
    };
}

bool AppController::copyLastTerminalCommand()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->semanticObserver || QGuiApplication::clipboard() == nullptr)
    {
        return false;
    }
    const terminal::SemanticTerminalSnapshot snapshot = tab->semanticObserver->snapshot();
    const auto block = std::ranges::find_if(snapshot.commandBlocks.rbegin(), snapshot.commandBlocks.rend(),
                                            [](const terminal::CommandBlock &candidate) {
                                                return !candidate.command.empty();
                                            });
    if (block == snapshot.commandBlocks.rend())
    {
        return false;
    }
    QGuiApplication::clipboard()->setText(utf8QString(block->command));
    return true;
}

bool AppController::copyLastTerminalCommandOutput()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->semanticObserver || QGuiApplication::clipboard() == nullptr)
    {
        return false;
    }
    const terminal::SemanticTerminalSnapshot snapshot = tab->semanticObserver->snapshot();
    const auto block = std::ranges::find_if(snapshot.commandBlocks.rbegin(), snapshot.commandBlocks.rend(),
                                            [](const terminal::CommandBlock &candidate) {
                                                return !candidate.command.empty();
                                            });
    if (block == snapshot.commandBlocks.rend() || !block->hasCompleteOutput() || block->retainedOutput.empty())
    {
        return false;
    }
    const QByteArray output(reinterpret_cast<const char *>(block->retainedOutput.data()),
                            static_cast<qsizetype>(block->retainedOutput.size()));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(output));
    return true;
}

bool AppController::activateTerminalPane(const QString &paneId)
{
    TerminalTab *tab = findTabForPane(paneId);
    if (tab == nullptr)
    {
        return false;
    }
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(tab->workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const bool changed =
        m_activeTabId != tab->workspaceId || m_focusedTabId != tab->id || workspace->activePaneId != utf8String(paneId);
    m_activeTabId = tab->workspaceId;
    m_focusedTabId = tab->id;
    workspace->activePaneId = utf8String(paneId);
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tab->workspaceId);
    m_terminal = m_terminalViewports.value(paneId);
    if (changed)
    {
        static_cast<void>(persistTerminalWorkspaces());
        emitActiveTerminalContextChanged();
        emit terminalTabsChanged();
    }
    return true;
}

bool AppController::splitActiveTerminal(const QString &orientation, const bool duplicateActive)
{
    TerminalTab *source = activeTab();
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (source == nullptr || workspace == nullptr || m_tabs.size() >= maximumTerminalTabs
        || workspace->restoreIntents.size() >= workbench::maximumTerminalPanesPerWorkspace)
    {
        return false;
    }
    if (duplicateActive && source->kind == TerminalTabKind::Ssh && source->sourceProfileId.isEmpty())
    {
        return false;
    }
    workbench::TerminalSplitOrientation splitOrientation;
    if (orientation == QStringLiteral("horizontal"))
    {
        splitOrientation = workbench::TerminalSplitOrientation::Horizontal;
    }
    else if (orientation == QStringLiteral("vertical"))
    {
        splitOrientation = workbench::TerminalSplitOrientation::Vertical;
    }
    else
    {
        return false;
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = source->workspaceId;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    workbench::TerminalRestoreKind restoreKind = workbench::TerminalRestoreKind::Local;
    if (duplicateActive && source->kind == TerminalTabKind::Ssh)
    {
        tab->kind = TerminalTabKind::Ssh;
        tab->title = source->title;
        tab->identity = source->identity;
        tab->address = source->address;
        tab->sourceProfileId = source->sourceProfileId;
        tab->status = tr("SSH pane duplicated; reconnecting...");
        tab->keywordHighlightRules = source->keywordHighlightRules;
        tab->keywordHighlightEnabled = source->keywordHighlightEnabled;
        applyWorkspaceState(*tab);
        tab->ssh = std::make_unique<ssh::SshTerminalSession>();
        restoreKind = tab->sourceProfileId.isEmpty() ? workbench::TerminalRestoreKind::Transient
                                                     : workbench::TerminalRestoreKind::SshProfile;
    }
    else
    {
        tab->kind = TerminalTabKind::Local;
        tab->title = tr("PowerShell %1").arg(m_nextLocalTabNumber++);
        tab->status = tr("Starting local terminal...");
        tab->local = m_localSessionFactory();
        if (!tab->local)
        {
            return false;
        }
    }

    const QString tabId = tab->id;
    const QString paneId = tab->paneId;
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::splitTerminalPane(*workspace, utf8String(source->paneId),
                                      utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                                      utf8String(paneId),
                                      {.id = utf8String(tabId),
                                       .profileId = utf8String(tab->sourceProfileId),
                                       .title = utf8String(tab->title),
                                       .kind = restoreKind},
                                      splitOrientation))
    {
        return false;
    }
    workspace->activePaneId = utf8String(paneId);
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    if (tab->local)
    {
        tab->local->setOutputSink(tab->outputSink);
        connectLocalTabSignals(*tab);
    }
    else
    {
        tab->ssh->setOutputSink(tab->outputSink);
        connectSshTabSignals(*tab);
    }
    m_tabs.push_back(std::move(tab));
    m_focusedTabId = tabId;
    if (!persistTerminalWorkspaces())
    {
        *workspace = previous;
        m_tabs.pop_back();
        m_focusedTabId = source->id;
        return false;
    }

    TerminalTab *created = findTab(tabId);
    if (created != nullptr && created->local)
    {
        const std::error_code error = created->local->start({.columns = 100, .rows = 30});
        if (error)
        {
            created->status = tr("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        }
    }
    else if (created != nullptr && created->ssh && !created->sourceProfileId.isEmpty())
    {
        created->reconnectPending = true;
        m_aiActionToolDispatcher.clearSession(
            {.sessionId = utf8String(created->id), .sessionGeneration = created->reconnectGeneration});
        attemptSshReconnect(created->id, ++created->reconnectGeneration);
    }
    emit terminalTabsChanged();
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::closeActiveTerminalPane()
{
    TerminalTab *tab = activeTab();
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (tab == nullptr || workspace == nullptr)
    {
        return false;
    }
    if (workspace->restoreIntents.size() == 1)
    {
        return closeTerminalTab(workspace->id.empty() ? QString{} : utf8QString(workspace->id));
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::closeTerminalPane(*workspace, utf8String(tab->paneId)))
    {
        return false;
    }
    TerminalTab *next = findTabForPane(utf8QString(workspace->activePaneId));
    if (next == nullptr || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    if (tab->local)
    {
        tab->local->stop();
    }
    if (tab->ssh)
    {
        tab->ssh->stop();
    }
    if (tab->sessionLog)
    {
        tab->sessionLog->stop();
    }
    stopSftpSession(*tab);
    m_terminalViewports.remove(tab->paneId);
    const QString closedId = tab->id;
    std::erase_if(m_tabs, [&closedId](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id == closedId;
    });
    m_focusedTabId = next->id;
    emit terminalTabsChanged();
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::focusRelativeTerminalPane(const int offset)
{
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    const TerminalTab *tab = activeTab();
    if (workspace == nullptr || tab == nullptr || offset == 0)
    {
        return false;
    }
    const std::vector<std::string> panes = workbench::terminalPaneOrder(*workspace);
    const auto current = std::ranges::find(panes, utf8String(tab->paneId));
    if (current == panes.end() || panes.empty())
    {
        return false;
    }
    const auto index = static_cast<std::ptrdiff_t>(std::distance(panes.begin(), current));
    const auto count = static_cast<std::ptrdiff_t>(panes.size());
    const auto next = (index + static_cast<std::ptrdiff_t>(offset % static_cast<int>(count)) + count) % count;
    return activateTerminalPane(utf8QString(panes[static_cast<std::size_t>(next)]));
}

bool AppController::resizeActiveTerminalPane(const qreal delta)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    const TerminalTab *tab = activeTab();
    if (workspace == nullptr || tab == nullptr || !std::isfinite(delta) || qFuzzyIsNull(delta))
    {
        return false;
    }
    const std::string paneId = utf8String(tab->paneId);
    const auto parent = std::ranges::find_if(workspace->nodes, [&paneId](const workbench::TerminalLayoutNode &node) {
        return node.kind == workbench::TerminalLayoutNodeKind::Split
               && (node.firstChildId == paneId || node.secondChildId == paneId);
    });
    if (parent == workspace->nodes.end())
    {
        return false;
    }
    const double adjusted = parent->ratio + (parent->firstChildId == paneId ? delta : -delta);
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::resizeTerminalSplit(*workspace, parent->id, adjusted) || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    return true;
}

bool AppController::setTerminalSplitRatio(const QString &splitNodeId, const qreal ratio)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (workspace == nullptr || !std::isfinite(ratio))
    {
        return false;
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::resizeTerminalSplit(*workspace, utf8String(splitNodeId), ratio) || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    return true;
}

bool AppController::swapActiveTerminalPane(const int offset)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    TerminalTab *active = activeTab();
    if (workspace == nullptr || active == nullptr || offset == 0)
    {
        return false;
    }
    const std::vector<std::string> panes = workbench::terminalPaneOrder(*workspace);
    const auto current = std::ranges::find(panes, utf8String(active->paneId));
    if (current == panes.end())
    {
        return false;
    }
    const auto index = static_cast<std::ptrdiff_t>(std::distance(panes.begin(), current));
    const auto targetIndex = index + (offset < 0 ? -1 : 1);
    if (targetIndex < 0 || static_cast<std::size_t>(targetIndex) >= panes.size())
    {
        return false;
    }
    const QString otherPaneId = utf8QString(panes[static_cast<std::size_t>(targetIndex)]);
    TerminalTab *other = findTabForPane(otherPaneId);
    if (other == nullptr)
    {
        return false;
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::swapTerminalPanes(*workspace, utf8String(active->paneId), utf8String(other->paneId)))
    {
        return false;
    }
    std::swap(active->paneId, other->paneId);
    workspace->activePaneId = utf8String(active->paneId);
    if (!persistTerminalWorkspaces())
    {
        std::swap(active->paneId, other->paneId);
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    showAllTerminalViewports();
    return true;
}

void AppController::searchTerminal(const QString &query, const bool backwards, const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (query.isEmpty())
    {
        clearTerminalSearch();
        return;
    }

    tab->searchQuery = query;
    tab->searchCaseSensitive = caseSensitive;
    if (tab->ssh)
    {
        tab->ssh->search(query, backwards, caseSensitive);
    }
    else if (tab->local)
    {
        tab->local->search(query, backwards, caseSensitive);
    }
    emit terminalSearchChanged();
}

void AppController::clearTerminalSearch()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->searchQuery.clear();
    tab->searchCurrent = 0;
    tab->searchTotal = 0;
    if (tab->ssh)
    {
        tab->ssh->clearSearch();
    }
    else if (tab->local)
    {
        tab->local->clearSearch();
    }
    emit terminalSearchChanged();
}

bool AppController::toggleTerminalWorkbench(const QString &page)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr
        || (page != QStringLiteral("history") && page != QStringLiteral("scripts") && page != QStringLiteral("sftp")
            && page != QStringLiteral("notes") && page != QStringLiteral("ai")))
    {
        return false;
    }
    if (page == QStringLiteral("sftp")
        && (tab->kind != TerminalTabKind::Ssh || tab->sshPhase != ssh::SshConnectionPhase::Connected))
    {
        return false;
    }

    if (tab->workbenchOpen && tab->workbenchPage == page)
    {
        tab->workbenchOpen = false;
        if (page == QStringLiteral("sftp"))
        {
            stopSftpSession(*tab);
        }
    }
    else
    {
        if (tab->workbenchOpen && tab->workbenchPage == QStringLiteral("sftp"))
        {
            stopSftpSession(*tab);
        }
        tab->workbenchPage = page;
        tab->workbenchOpen = true;
        if (page == QStringLiteral("sftp"))
        {
            (void)startSftpSession(*tab);
        }
        else if (page == QStringLiteral("ai"))
        {
            refreshConfiguredAiModels();
            static_cast<void>(buildAiContext(*tab, false));
            emit aiConversationChanged();
        }
    }
    emit terminalTabsChanged();
    emit sftpChanged();
    persistWorkspaceState(*tab);
    return true;
}

void AppController::closeTerminalWorkbench()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->workbenchOpen)
    {
        return;
    }
    tab->workbenchOpen = false;
    if (tab->workbenchPage == QStringLiteral("sftp"))
    {
        stopSftpSession(*tab);
    }
    emit terminalTabsChanged();
    emit sftpChanged();
}

void AppController::setTerminalWorkbenchWidth(const qreal width)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    const qreal bounded = std::clamp(width, qreal{320.0}, qreal{800.0});
    if (qFuzzyCompare(tab->workbenchWidth, bounded))
    {
        return;
    }
    tab->workbenchWidth = bounded;
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

void AppController::moveTerminalWorkbench()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->workbenchSide =
        tab->workbenchSide == QStringLiteral("left") ? QStringLiteral("right") : QStringLiteral("left");
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

void AppController::toggleTerminalComposer()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->composerOpen = !tab->composerOpen;
    emit terminalTabsChanged();
}

void AppController::setTerminalComposerHeight(const qreal height)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    const qreal bounded = std::clamp(height, qreal{104.0}, qreal{360.0});
    if (qFuzzyCompare(tab->composerHeight, bounded))
    {
        return;
    }
    tab->composerHeight = bounded;
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

bool AppController::copyActiveTerminalAddress()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->address.isEmpty() || m_terminal == nullptr)
    {
        return false;
    }
    m_terminal->setClipboardText(tab->address);
    return true;
}

bool AppController::copyActiveSftpPath()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpPath.isEmpty() || m_terminal == nullptr)
    {
        return false;
    }
    m_terminal->setClipboardText(tab->sftpPath);
    return true;
}

bool AppController::insertTerminalCommand(const QString &command)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !validTerminalCommand(command))
    {
        return false;
    }
    queuePaste(normalizedQuickCommandText(command).toUtf8());
    return true;
}

bool AppController::openTerminalLink(const QString &uri)
{
    const QUrl url = QUrl::fromUserInput(uri);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")))
    {
        return false;
    }
    return QDesktopServices::openUrl(url);
}

bool AppController::runTerminalCommand(const QString &command)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !validTerminalCommand(command))
    {
        return false;
    }
    QString normalized = normalizedQuickCommandText(command);
    QByteArray bytes = normalized.toUtf8();
    bytes.replace('\n', '\r');
    if (bytes.isEmpty() || bytes.back() != '\r')
    {
        bytes.append('\r');
    }
    appendCapturedHistory(*tab, normalized);
    if (tab->scriptRecorder.state() == workbench::ScriptRecorderState::Recording
        && tab->scriptRecorder.recordCommand(utf8String(normalized), recorderNow()))
    {
        emit terminalTabsChanged();
    }
    tab->inputHistoryBuffer.clear();
    tab->inputHistoryBufferReliable = true;
    dispatchInput(*tab, bytes);
    return true;
}

bool AppController::startTerminalLog(const QString &localFileUrl)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sessionLog == nullptr)
    {
        return false;
    }
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (!tab->sessionLog->start(path))
    {
        tab->status = tr("Session log could not be started.");
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tab->status);
        }
        emit terminalTabsChanged();
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

void AppController::stopTerminalLog()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sessionLog == nullptr)
    {
        return;
    }
    tab->sessionLog->stop();
    emit terminalTabsChanged();
}

bool AppController::setActiveKeywordHighlightEnabled(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->keywordHighlightEnabled == enabled)
    {
        return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
    }
    const bool previous = tab->keywordHighlightEnabled;
    tab->keywordHighlightEnabled = enabled;
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightEnabled = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::saveActiveKeywordHighlightRule(const QString &id, const QString &pattern, const QString &foreground,
                                                   const QString &background, const bool enabled,
                                                   const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh)
    {
        return false;
    }
    ssh::SshKeywordHighlightRule rule{
        .id = utf8String(id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed()),
        .pattern = utf8String(pattern),
        .foreground = utf8String(foreground.trimmed()),
        .background = utf8String(background.trimmed()),
        .enabled = enabled,
        .caseSensitive = caseSensitive,
    };
    if (!ssh::validKeywordHighlightRule(rule))
    {
        return false;
    }
    const std::vector<ssh::SshKeywordHighlightRule> previous = tab->keywordHighlightRules;
    const auto existing = std::ranges::find(tab->keywordHighlightRules, rule.id, &ssh::SshKeywordHighlightRule::id);
    if (existing == tab->keywordHighlightRules.end())
    {
        if (tab->keywordHighlightRules.size() >= ui::maximumKeywordRules)
        {
            return false;
        }
        tab->keywordHighlightRules.push_back(std::move(rule));
    }
    else
    {
        *existing = std::move(rule);
    }
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightRules = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::deleteActiveKeywordHighlightRule(const QString &id)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh)
    {
        return false;
    }
    const std::string normalizedId = utf8String(id.trimmed());
    const auto existing =
        std::ranges::find(tab->keywordHighlightRules, normalizedId, &ssh::SshKeywordHighlightRule::id);
    if (existing == tab->keywordHighlightRules.end())
    {
        return false;
    }
    const std::vector<ssh::SshKeywordHighlightRule> previous = tab->keywordHighlightRules;
    tab->keywordHighlightRules.erase(existing);
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightRules = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::setActiveTerminalEncoding(const QString &encoding)
{
    TerminalTab *tab = activeTab();
    const auto parsed = terminal::terminalEncodingFromToken(encoding);
    if (tab == nullptr || !tab->ssh || !parsed)
    {
        return false;
    }
    tab->terminalEncoding = terminal::terminalEncodingToken(*parsed);
    tab->ssh->setEncoding(tab->terminalEncoding);
    emit terminalTabsChanged();
    return true;
}

bool AppController::setActiveTerminalAppearance(const QString &fontFamily, const int fontSize, const bool ligatures,
                                                const qreal backgroundOpacity, const QString &cursor,
                                                const QString &foreground, const QString &background)
{
    TerminalTab *tab = activeTab();
    const QString normalizedCursor = cursor.trimmed().toLower();
    const QColor foregroundColor(foreground);
    const QColor backgroundColor(background);
    if (tab == nullptr || fontFamily.trimmed().isEmpty() || fontSize < 8 || fontSize > 32 || backgroundOpacity < 0.0
        || backgroundOpacity > 1.0
        || (normalizedCursor != QStringLiteral("terminal") && normalizedCursor != QStringLiteral("block")
            && normalizedCursor != QStringLiteral("bar") && normalizedCursor != QStringLiteral("underline"))
        || !foregroundColor.isValid() || !backgroundColor.isValid())
    {
        return false;
    }
    tab->sessionFontFamily = fontFamily.trimmed();
    tab->sessionFontSize = fontSize;
    tab->sessionLigatures = ligatures;
    tab->sessionBackgroundOpacity = backgroundOpacity;
    tab->sessionCursor = normalizedCursor;
    tab->sessionForeground = foregroundColor.name(QColor::HexRgb);
    tab->sessionBackground = backgroundColor.name(QColor::HexRgb);
    emit terminalTabsChanged();
    return true;
}

bool AppController::resetActiveTerminalAppearance()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sessionFontFamily.clear();
    tab->sessionFontSize = 0;
    tab->sessionLigatures = m_settings.terminalLigatures;
    tab->sessionBackgroundOpacity = -1.0;
    tab->sessionCursor.clear();
    tab->sessionForeground.clear();
    tab->sessionBackground.clear();
    emit terminalTabsChanged();
    return true;
}

bool AppController::startTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !tab->scriptRecorder.start(recorderNow()))
    {
        return false;
    }
    ++tab->scriptPlaybackGeneration;
    tab->scriptPlaybackActive = false;
    tab->recordingStartedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    emit terminalTabsChanged();
    return true;
}

bool AppController::pauseTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.pause())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::resumeTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.resume(recorderNow()))
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::stopTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.stop())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::replayTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || tab->scriptRecorder.state() != workbench::ScriptRecorderState::Review
        || tab->scriptRecorder.steps().empty() || tab->scriptPlaybackActive)
    {
        return false;
    }
    tab->scriptPlaybackActive = true;
    const std::uint64_t generation = ++tab->scriptPlaybackGeneration;
    const QString tabId = tab->id;
    emit terminalTabsChanged();
    replayRecordedScriptStep(tabId, 0, generation);
    return true;
}

bool AppController::copyTerminalScriptRecording()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->scriptRecorder.steps().empty())
    {
        return false;
    }
    QJsonArray steps;
    for (const workbench::RecordedScriptStep &step : tab->scriptRecorder.steps())
    {
        if (step.kind == workbench::RecordedScriptStepKind::Send)
        {
            steps.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("send")},
                                     {QStringLiteral("value"), utf8QString(step.command)}});
        }
        else
        {
            steps.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("delay")},
                                     {QStringLiteral("milliseconds"), static_cast<qint64>(step.delayMilliseconds)}});
        }
    }
    const QJsonDocument document(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("steps"), steps}});
    QGuiApplication::clipboard()->setText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
    return true;
}

bool AppController::saveTerminalScriptRecordingAsScript(const QString &name, const QString &description)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->scriptRecorder.state() != workbench::ScriptRecorderState::Review
        || tab->scriptRecorder.steps().empty())
    {
        setQuickCommandOperationError(tr("Stop and review a non-empty recording before saving it as a script."));
        return false;
    }

    QVariantList steps;
    for (const workbench::RecordedScriptStep &step : tab->scriptRecorder.steps())
    {
        if (step.kind == workbench::RecordedScriptStepKind::Send)
        {
            steps.append(QVariantMap{{QStringLiteral("command"), utf8QString(step.command)},
                                     {QStringLiteral("continuation"), QStringLiteral("immediate")},
                                     {QStringLiteral("outputMarker"), QString{}},
                                     {QStringLiteral("timeoutMs"), 30'000}});
        }
    }
    if (steps.isEmpty())
    {
        setQuickCommandOperationError(tr("The recording does not contain any command actions."));
        return false;
    }
    return saveScript(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("description"), description},
                                  {QStringLiteral("shell"), QStringLiteral("any")},
                                  {QStringLiteral("variables"), QVariantList{}},
                                  {QStringLiteral("steps"), steps}});
}

void AppController::clearTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    ++tab->scriptPlaybackGeneration;
    tab->scriptPlaybackActive = false;
    tab->recordingStartedUtcMs = 0;
    tab->scriptRecorder.clear();
    emit terminalTabsChanged();
}

bool AppController::saveScript(const QVariantMap &value)
{
    const auto shellScope = quickCommandShellScope(value.value(QStringLiteral("shell")).toString());
    const QVariantList variableValues = value.value(QStringLiteral("variables")).toList();
    const QVariantList stepValues = value.value(QStringLiteral("steps")).toList();
    if (!shellScope || variableValues.size() > static_cast<qsizetype>(workbench::maximumScriptVariableCount)
        || stepValues.size() > static_cast<qsizetype>(workbench::maximumScriptStepCount))
    {
        setQuickCommandOperationError(tr("The script definition is invalid."));
        return false;
    }

    const QString suppliedId = value.value(QStringLiteral("id")).toString().trimmed();
    const std::int64_t now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    workbench::ScriptDefinition script{
        .id = suppliedId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()
                                   : utf8String(suppliedId),
        .name = utf8String(value.value(QStringLiteral("name")).toString().trimmed()),
        .description = utf8String(value.value(QStringLiteral("description")).toString().trimmed()),
        .shellScope = *shellScope,
        .createdUtcMs = now,
        .modifiedUtcMs = now,
    };
    script.variables.reserve(static_cast<std::size_t>(variableValues.size()));
    for (const QVariant &entry : variableValues)
    {
        const QVariantMap variableValue = entry.toMap();
        const auto type = parseScriptVariableType(variableValue.value(QStringLiteral("type")).toString());
        if (!type)
        {
            setQuickCommandOperationError(tr("The script contains an invalid variable."));
            return false;
        }
        std::vector<std::string> choices;
        const QVariantList choiceValues = variableValue.value(QStringLiteral("choices")).toList();
        choices.reserve(static_cast<std::size_t>(choiceValues.size()));
        for (const QVariant &choice : choiceValues)
        {
            choices.push_back(utf8String(choice.toString()));
        }
        script.variables.push_back({
            .name = utf8String(variableValue.value(QStringLiteral("name")).toString().trimmed()),
            .label = utf8String(variableValue.value(QStringLiteral("label")).toString().trimmed()),
            .type = *type,
            .defaultValue = utf8String(variableValue.value(QStringLiteral("defaultValue")).toString()),
            .choices = std::move(choices),
            .required = variableValue.value(QStringLiteral("required")).toBool(),
        });
    }
    script.steps.reserve(static_cast<std::size_t>(stepValues.size()));
    for (const QVariant &entry : stepValues)
    {
        const QVariantMap stepValue = entry.toMap();
        const auto continuation = parseScriptContinuation(stepValue.value(QStringLiteral("continuation")).toString());
        bool validTimeout = false;
        const qlonglong timeout = stepValue.value(QStringLiteral("timeoutMs")).toLongLong(&validTimeout);
        if (!continuation || !validTimeout || timeout < 0
            || std::cmp_greater(timeout, std::numeric_limits<std::uint32_t>::max()))
        {
            setQuickCommandOperationError(tr("The script contains an invalid step."));
            return false;
        }
        script.steps.push_back({
            .command = utf8String(normalizedQuickCommandText(stepValue.value(QStringLiteral("command")).toString())),
            .continuation = *continuation,
            .outputMarker = utf8String(stepValue.value(QStringLiteral("outputMarker")).toString()),
            .timeoutMs = static_cast<std::uint32_t>(timeout),
        });
    }
    if (!workbench::validScriptDefinition(script))
    {
        setQuickCommandOperationError(tr("The script definition is invalid."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    if (suppliedId.isEmpty())
    {
        if (candidate.size() >= workbench::maximumScriptCount)
        {
            setQuickCommandOperationError(tr("The script library has reached its 256 item limit."));
            return false;
        }
        candidate.push_back(std::move(script));
    }
    else
    {
        const auto existing = std::ranges::find(candidate, script.id, &workbench::ScriptDefinition::id);
        if (existing == candidate.end())
        {
            setQuickCommandOperationError(tr("The script no longer exists."));
            return false;
        }
        script.createdUtcMs = existing->createdUtcMs;
        *existing = std::move(script);
    }
    if (!m_scriptStore.save(candidate))
    {
        setQuickCommandOperationError(tr("The script could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

QVariantMap AppController::renderScript(const QString &id, const QVariantMap &values) const
{
    const auto existing = std::ranges::find(m_scripts, utf8String(id), &workbench::ScriptDefinition::id);
    if (existing == m_scripts.end())
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not-found")}};
    }
    workbench::ScriptVariableValues variables;
    variables.reserve(static_cast<std::size_t>(values.size()));
    for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator)
    {
        variables.emplace(utf8String(iterator.key()), utf8String(iterator.value().toString()));
    }
    const auto rendered = workbench::renderScript(*existing, variables);
    if (!rendered)
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), scriptRenderErrorToken(rendered.error())}};
    }
    QVariantList steps;
    steps.reserve(static_cast<qsizetype>(rendered->steps.size()));
    for (const workbench::RenderedScriptStep &step : rendered->steps)
    {
        steps.append(QVariantMap{{QStringLiteral("command"), utf8QString(step.command)},
                                 {QStringLiteral("continuation"), scriptContinuationToken(step.continuation)},
                                 {QStringLiteral("outputMarker"), utf8QString(step.outputMarker)},
                                 {QStringLiteral("timeoutMs"), step.timeoutMs}});
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("id"), utf8QString(rendered->id)},
            {QStringLiteral("name"), utf8QString(rendered->name)},
            {QStringLiteral("steps"), steps}};
}

bool AppController::runScript(const QString &id, const QVariantMap &values, const QString &targetSessionId)
{
    TerminalTab *tab = findTab(targetSessionId);
    const auto existing = std::ranges::find(m_scripts, utf8String(id), &workbench::ScriptDefinition::id);
    if (tab == nullptr || !tab->running || existing == m_scripts.end())
    {
        setQuickCommandOperationError(tr("Choose a running target terminal for the script."));
        return false;
    }
    workbench::ScriptVariableValues variables;
    variables.reserve(static_cast<std::size_t>(values.size()));
    for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator)
    {
        variables.emplace(utf8String(iterator.key()), utf8String(iterator.value().toString()));
    }
    auto rendered = workbench::renderScript(*existing, variables);
    if (!rendered)
    {
        setQuickCommandOperationError(tr("Fill every required script variable with a valid value."));
        return false;
    }
    auto commands = tab->scriptExecution.start(std::move(*rendered), utf8String(tab->id), scriptExecutionNow());
    if (!commands)
    {
        setQuickCommandOperationError(tab->scriptExecution.active()
                                          ? tr("A script is already running in this terminal.")
                                          : tr("The script could not be started."));
        return false;
    }
    m_quickCommandOperationError.clear();
    dispatchScriptCommands(*tab, *commands);
    emit quickCommandsChanged();
    emit terminalTabsChanged();
    return true;
}

bool AppController::cancelScript(const QString &targetSessionId)
{
    TerminalTab *tab = findTab(targetSessionId);
    if (tab == nullptr || !tab->scriptExecution.cancel())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::saveQuickCommand(const QString &id, const QString &name, const QString &command,
                                     const QString &description, const QString &shellScope)
{
    const QString normalizedName = name.trimmed();
    const QString normalizedCommand = normalizedQuickCommandText(command);
    const QString normalizedDescription = description.trimmed();
    const auto parsedShellScope = quickCommandShellScope(shellScope);
    if (normalizedName.isEmpty() || normalizedCommand.trimmed().isEmpty() || !parsedShellScope)
    {
        setQuickCommandOperationError(tr("Enter a name and command, then choose a valid shell scope."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::int64_t now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    workbench::ScriptDefinition script{
        .id = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() : utf8String(id),
        .name = utf8String(normalizedName),
        .description = utf8String(normalizedDescription),
        .shellScope = *parsedShellScope,
        .variables = {},
        .steps = {{.command = utf8String(normalizedCommand)}},
        .createdUtcMs = now,
        .modifiedUtcMs = now,
    };

    if (id.isEmpty())
    {
        candidate.push_back(std::move(script));
    }
    else
    {
        const auto existing = std::ranges::find(candidate, script.id, &workbench::ScriptDefinition::id);
        if (existing == candidate.end())
        {
            setQuickCommandOperationError(tr("The script no longer exists."));
            return false;
        }
        script.createdUtcMs = existing->createdUtcMs;
        *existing = std::move(script);
    }

    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist scripts";
        setQuickCommandOperationError(tr("The script could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::deleteQuickCommand(const QString &id)
{
    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::string commandId = utf8String(id);
    const auto existing = std::ranges::find(candidate, commandId, &workbench::ScriptDefinition::id);
    if (existing == candidate.end())
    {
        setQuickCommandOperationError(tr("The script no longer exists."));
        return false;
    }
    candidate.erase(existing);
    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist script deletion";
        setQuickCommandOperationError(tr("The script could not be deleted."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::moveQuickCommand(const QString &id, const int targetIndex)
{
    if (m_scripts.empty())
    {
        return false;
    }
    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::string commandId = utf8String(id);
    const auto existing = std::ranges::find(candidate, commandId, &workbench::ScriptDefinition::id);
    if (existing == candidate.end())
    {
        setQuickCommandOperationError(tr("The script no longer exists."));
        return false;
    }

    const auto sourceIndex = static_cast<std::size_t>(std::distance(candidate.begin(), existing));
    const auto boundedTarget =
        static_cast<std::size_t>(std::clamp(targetIndex, 0, static_cast<int>(candidate.size() - 1U)));
    if (sourceIndex == boundedTarget)
    {
        return true;
    }
    workbench::ScriptDefinition moved = std::move(candidate[sourceIndex]);
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
    candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(boundedTarget), std::move(moved));
    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist script order";
        setQuickCommandOperationError(tr("The script order could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::importQuickCommands(const QString &localFileUrl)
{
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (!QFileInfo(path).isFile())
    {
        setQuickCommandOperationError(tr("The script library could not be imported."));
        return false;
    }
    std::vector<workbench::ScriptDefinition> imported;
    const workbench::ScriptStore source(path);
    if (auto scripts = source.load())
    {
        imported = std::move(*scripts);
    }
    else if (scripts.error() == workbench::ScriptStoreError::unsupportedVersion)
    {
        const auto legacy = workbench::QuickCommandStore(path).load();
        if (!legacy)
        {
            setQuickCommandOperationError(tr("The script library could not be imported."));
            return false;
        }
        imported.reserve(legacy->size());
        std::ranges::transform(*legacy, std::back_inserter(imported), workbench::scriptFromQuickCommand);
    }
    else
    {
        setQuickCommandOperationError(tr("The script library could not be imported."));
        return false;
    }
    if (m_scripts.size() + imported.size() > workbench::maximumScriptCount)
    {
        setQuickCommandOperationError(tr("The imported script library exceeds the 256 item limit."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    QSet<QString> ids;
    ids.reserve(static_cast<qsizetype>(candidate.size() + imported.size()));
    for (const workbench::ScriptDefinition &script : candidate)
    {
        ids.insert(utf8QString(script.id));
    }
    for (workbench::ScriptDefinition &script : imported)
    {
        QString id = utf8QString(script.id);
        if (ids.contains(id))
        {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            script.id = utf8String(id);
        }
        ids.insert(id);
        candidate.push_back(std::move(script));
    }
    if (!m_scriptStore.save(candidate))
    {
        setQuickCommandOperationError(tr("The imported script library could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    setQuickCommandOperationError({});
    return true;
}

bool AppController::exportQuickCommands(const QString &localFileUrl)
{
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    const workbench::ScriptStore destination(path);
    if (!destination.save(m_scripts))
    {
        setQuickCommandOperationError(tr("The script library could not be exported."));
        return false;
    }
    setQuickCommandOperationError({});
    return true;
}

void AppController::refreshNotes()
{
    const auto entries = m_noteStore.entries();
    if (!entries)
    {
        setNoteOperationError(tr("The local notes folder could not be read."));
        return;
    }
    QVariantList values;
    values.reserve(static_cast<qsizetype>(entries->size()));
    for (const workbench::NoteEntry &entry : *entries)
    {
        values.append(QVariantMap{{QStringLiteral("path"), entry.path},
                                  {QStringLiteral("name"), entry.name},
                                  {QStringLiteral("folder"), entry.folder},
                                  {QStringLiteral("size"), QVariant::fromValue<qulonglong>(entry.size)},
                                  {QStringLiteral("modifiedUtcMs"), entry.modifiedUtcMs}});
    }
    m_notes = std::move(values);
    m_noteOperationError.clear();
    emit notesChanged();
}

bool AppController::openNote(const QString &relativePath, const bool discardUnsavedChanges)
{
    if (m_activeNoteDirty && !discardUnsavedChanges)
    {
        if (relativePath == m_activeNotePath)
        {
            return true;
        }
        setNoteOperationError(tr("Save or discard the current note before opening another one."));
        return false;
    }
    const auto content = m_noteStore.read(relativePath);
    if (!content)
    {
        setNoteOperationError(tr("The note could not be opened."));
        return false;
    }
    m_activeNotePath = relativePath;
    m_activeNoteContent = *content;
    m_activeNoteDirty = false;
    m_noteOperationError.clear();
    emit notesChanged();
    return true;
}

void AppController::updateActiveNoteContent(const QString &content)
{
    if (m_activeNotePath.isEmpty() || m_activeNoteContent == content)
    {
        return;
    }
    m_activeNoteContent = content;
    m_activeNoteDirty = true;
    emit notesChanged();
}

bool AppController::saveActiveNote()
{
    if (m_activeNotePath.isEmpty())
    {
        setNoteOperationError(tr("Open a note before saving."));
        return false;
    }
    if (const auto saved = m_noteStore.save(m_activeNotePath, m_activeNoteContent); !saved)
    {
        setNoteOperationError(saved.error() == workbench::NoteStoreError::noteTooLarge
                                  ? tr("The note exceeds the 2 MiB limit.")
                                  : tr("The note could not be saved."));
        return false;
    }
    m_activeNoteDirty = false;
    m_noteOperationError.clear();
    refreshNotes();
    return true;
}

bool AppController::discardActiveNoteChanges()
{
    if (m_activeNotePath.isEmpty())
    {
        return false;
    }
    return openNote(m_activeNotePath, true);
}

bool AppController::createNote(const QString &relativePath)
{
    if (m_activeNoteDirty)
    {
        setNoteOperationError(tr("Save or discard the current note before creating another one."));
        return false;
    }
    if (const auto saved = m_noteStore.save(relativePath, QString{}); !saved)
    {
        setNoteOperationError(tr("The note could not be created. Use a safe relative path ending in .md."));
        return false;
    }
    refreshNotes();
    return openNote(relativePath, true);
}

bool AppController::createNoteFolder(const QString &relativePath)
{
    if (const auto created = m_noteStore.createFolder(relativePath); !created)
    {
        setNoteOperationError(tr("The note folder could not be created."));
        return false;
    }
    refreshNotes();
    return true;
}

bool AppController::renameNoteEntry(const QString &sourceRelativePath, const QString &destinationRelativePath)
{
    if (const auto renamed = m_noteStore.renameEntry(sourceRelativePath, destinationRelativePath); !renamed)
    {
        setNoteOperationError(tr("The note or folder could not be moved or renamed."));
        return false;
    }
    if (m_activeNotePath == sourceRelativePath)
    {
        m_activeNotePath = destinationRelativePath;
    }
    else if (m_activeNotePath.startsWith(sourceRelativePath + QLatin1Char('/')))
    {
        m_activeNotePath = destinationRelativePath + m_activeNotePath.sliced(sourceRelativePath.size());
    }
    refreshNotes();
    return true;
}

bool AppController::deleteNoteEntry(const QString &relativePath)
{
    if ((m_activeNotePath == relativePath || m_activeNotePath.startsWith(relativePath + QLatin1Char('/')))
        && m_activeNoteDirty)
    {
        setNoteOperationError(tr("Discard the active note changes before deleting it."));
        return false;
    }
    if (const auto removed = m_noteStore.removeEntry(relativePath); !removed)
    {
        setNoteOperationError(tr("The note or folder could not be deleted."));
        return false;
    }
    if (m_activeNotePath == relativePath || m_activeNotePath.startsWith(relativePath + QLatin1Char('/')))
    {
        m_activeNotePath.clear();
        m_activeNoteContent.clear();
        m_activeNoteDirty = false;
    }
    refreshNotes();
    return true;
}

void AppController::searchNotes(const QString &query)
{
    const std::uint64_t requestId = ++m_noteSearchRequestId;
    const QString normalized = query.trimmed();
    if (normalized.isEmpty())
    {
        m_noteSearchResults.clear();
        m_noteSearchState = QStringLiteral("idle");
        emit notesChanged();
        return;
    }
    m_noteSearchState = QStringLiteral("loading");
    m_noteOperationError.clear();
    emit notesChanged();

    const QString rootPath = m_noteStore.rootPath();
    const QString searchError = tr("The local notes search could not be completed.");
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, rootPath, normalized, requestId, searchError] {
        NoteSearchResults results;
        QString error;
        try
        {
            auto searched = workbench::NoteStore(rootPath).search(normalized);
            if (searched)
            {
                results = std::move(*searched);
            }
            else
            {
                error = searchError;
            }
        }
        catch (const std::bad_alloc &)
        {
            error = searchError;
        }
        if (self)
        {
            emit self->noteSearchTaskCompleted(requestId, std::move(results), error);
        }
    });
}

bool AppController::importNote(const QString &localFileUrl, const QString &destinationFolder)
{
    if (m_activeNoteDirty)
    {
        setNoteOperationError(tr("Save or discard the current note before importing another one."));
        return false;
    }
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    const auto imported = m_noteStore.importNote(path, destinationFolder);
    if (!imported)
    {
        setNoteOperationError(tr("Only valid UTF-8 Markdown notes up to 2 MiB can be imported."));
        return false;
    }
    refreshNotes();
    return openNote(*imported, true);
}

bool AppController::exportActiveNote(const QString &localFileUrl)
{
    if (m_activeNotePath.isEmpty())
    {
        return false;
    }
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (const auto exported = m_noteStore.exportNote(m_activeNotePath, path); !exported)
    {
        setNoteOperationError(tr("The note could not be exported."));
        return false;
    }
    setNoteOperationError({});
    return true;
}

void AppController::refreshTerminalHistory()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (tab->kind == TerminalTabKind::Ssh)
    {
        if (tab->ssh == nullptr || !tab->running)
        {
            tab->historyState = QStringLiteral("error");
            tab->historyError = tr("Connect the SSH terminal before reading remote history.");
            emit terminalHistoryChanged();
            return;
        }
        tab->historyState = QStringLiteral("loading");
        tab->historyError.clear();
        const std::uint64_t requestId = ++tab->historyRequestId;
        emit terminalHistoryChanged();
        tab->ssh->requestShellHistory(requestId);
        return;
    }

    const QString historyPath = workbench::defaultPowerShellHistoryPath();
    if (historyPath.isEmpty())
    {
        tab->historyState = QStringLiteral("error");
        tab->historyError = tr("The PowerShell history location is unavailable.");
        emit terminalHistoryChanged();
        return;
    }

    tab->historyState = QStringLiteral("loading");
    tab->historyError.clear();
    const QString tabId = tab->id;
    const std::uint64_t requestId = ++tab->historyRequestId;
    const QString historyReadError = tr("PowerShell history could not be read.");
    emit terminalHistoryChanged();

    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, historyPath, tabId, requestId, historyReadError] {
        std::vector<workbench::ShellHistoryEntry> entries;
        bool readFailed = true;
        try
        {
            auto result = workbench::readPowerShellHistory(historyPath);
            if (result)
            {
                entries = std::move(*result);
                readFailed = false;
            }
        }
        catch (const std::bad_alloc &)
        {
            readFailed = true;
        }
        if (self)
        {
            emit self->terminalHistoryTaskCompleted(tabId, requestId, std::move(entries),
                                                    readFailed ? historyReadError : QString{});
        }
    });
}

void AppController::setTerminalTelemetryVisible(const bool visible)
{
    if (m_terminalTelemetryVisible == visible)
    {
        return;
    }
    m_terminalTelemetryVisible = visible;
    updateTelemetryVisibility();
}

void AppController::refreshRemoteTelemetry()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->ssh == nullptr || !tab->running)
    {
        return;
    }
    tab->telemetryState = QStringLiteral("loading");
    emit remoteTelemetryChanged();
    tab->ssh->refreshRemoteTelemetry();
}

void AppController::refreshSftpDirectory()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->sftpSession == nullptr && tab->workbenchPage == QStringLiteral("sftp"))
    {
        (void)startSftpSession(*tab);
        return;
    }
    if (tab != nullptr && tab->sftpSession != nullptr)
    {
        requestSftpDirectory(*tab, tab->sftpPath);
    }
}

bool AppController::navigateSftpDirectory(const QString &remotePath)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr)
    {
        return false;
    }
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    if (!normalized)
    {
        return false;
    }
    requestSftpDirectory(*tab, utf8QString(*normalized));
    return true;
}

bool AppController::navigateSftpHome()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr || tab->sftpHomePath.isEmpty())
    {
        return false;
    }
    requestSftpDirectory(*tab, tab->sftpHomePath);
    return true;
}

bool AppController::navigateSftpParent()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr)
    {
        return false;
    }
    requestSftpDirectory(*tab, utf8QString(sftp::parentRemotePath(utf8String(tab->sftpPath))));
    return true;
}

bool AppController::toggleActiveSftpBookmark()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty() || tab->sftpPath.isEmpty())
    {
        return false;
    }
    workbench::WorkspaceState candidate = m_workspaceState;
    workbench::ProfileWorkspaceState &state =
        workbench::ensureProfileWorkspaceState(candidate, utf8String(tab->sourceProfileId));
    (void)workbench::toggleBookmarkedRemotePath(state, utf8String(tab->sftpPath));
    if (!saveWorkspaceStateCandidate(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist SFTP path bookmark";
        return false;
    }
    m_workspaceState = std::move(candidate);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpViewMode(const QString &mode)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (mode != QStringLiteral("list") && mode != QStringLiteral("tree")))
    {
        return false;
    }
    if (tab->sftpViewMode == mode)
    {
        return true;
    }
    tab->sftpViewMode = mode;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setViewMode(mode);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpSort(const QString &column, const bool ascending)
{
    TerminalTab *tab = activeTab();
    const QString normalized = column.trimmed().toLower();
    if (tab == nullptr
        || (normalized != QStringLiteral("name") && normalized != QStringLiteral("modified")
            && normalized != QStringLiteral("size") && normalized != QStringLiteral("type")))
    {
        return false;
    }
    tab->sftpSortColumn = normalized;
    tab->sftpSortAscending = ascending;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setSortColumn(normalized);
        tab->sftpModel->setSortAscending(ascending);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpDirectoriesFirst(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sftpDirectoriesFirst = enabled;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setDirectoriesFirst(enabled);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpVisibleColumns(const bool modified, const bool size, const bool type)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sftpShowModifiedColumn = modified;
    tab->sftpShowSizeColumn = size;
    tab->sftpShowTypeColumn = type;
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpFilenameEncoding(const QString &encoding)
{
    TerminalTab *tab = activeTab();
    const QString normalized = encoding.trimmed().toLower();
    if (tab == nullptr || (normalized != QStringLiteral("utf-8") && normalized != QStringLiteral("gb18030")))
    {
        return false;
    }
    if (tab->sftpFilenameEncoding == normalized)
    {
        return true;
    }
    tab->sftpFilenameEncoding = normalized;
    persistWorkspaceState(*tab);
    stopSftpSession(*tab);
    const bool started = startSftpSession(*tab);
    emit sftpChanged();
    return started;
}

bool AppController::navigateSftpToTerminalDirectory()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr || tab->terminalWorkingDirectory.isEmpty())
    {
        return false;
    }
    requestSftpDirectory(*tab, tab->terminalWorkingDirectory);
    return true;
}

bool AppController::setSftpFollowTerminalDirectory(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (enabled && tab->kind != TerminalTabKind::Ssh))
    {
        return false;
    }
    if (tab->followTerminalDirectory == enabled)
    {
        return true;
    }
    tab->followTerminalDirectory = enabled;
    persistWorkspaceState(*tab);
    emit sftpChanged();
    if (enabled && tab->sftpSession != nullptr && !tab->terminalWorkingDirectory.isEmpty()
        && tab->terminalWorkingDirectory != tab->sftpPath)
    {
        requestSftpDirectory(*tab, tab->terminalWorkingDirectory);
    }
    return true;
}

bool AppController::createSftpDirectory(const QString &name)
{
    TerminalTab *tab = activeTab();
    const std::string remoteName = utf8String(name.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(remoteName))
    {
        return false;
    }
    const auto path = sftp::joinRemotePath(utf8String(tab->sftpPath), remoteName);
    if (!path)
    {
        return false;
    }
    tab->sftpSession->requestCreateDirectory(++tab->sftpRequestId, utf8QString(*path));
    return true;
}

bool AppController::createSftpFile(const QString &name)
{
    TerminalTab *tab = activeTab();
    const std::string remoteName = utf8String(name.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(remoteName))
    {
        return false;
    }
    const auto path = sftp::joinRemotePath(utf8String(tab->sftpPath), remoteName);
    if (!path)
    {
        return false;
    }
    tab->sftpSession->requestCreateFile(++tab->sftpRequestId, utf8QString(*path));
    return true;
}

bool AppController::renameSftpEntry(const QString &remotePath, const QString &newName)
{
    TerminalTab *tab = activeTab();
    const std::string source = utf8String(remotePath);
    const std::string replacement = utf8String(newName.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(replacement))
    {
        return false;
    }
    const auto destination = sftp::joinRemotePath(sftp::parentRemotePath(source), replacement);
    if (!destination)
    {
        return false;
    }
    tab->sftpSession->requestRenameEntry(++tab->sftpRequestId, remotePath, utf8QString(*destination));
    return true;
}

bool AppController::removeSftpEntry(const QString &remotePath, const bool directory)
{
    TerminalTab *tab = activeTab();
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    if (tab == nullptr || tab->sftpSession == nullptr || !normalized || *normalized == "/")
    {
        return false;
    }
    tab->sftpSession->requestRemoveEntry(++tab->sftpRequestId, utf8QString(*normalized), directory);
    return true;
}

bool AppController::enqueueSftpDownload(const QString &remotePath, const QString &localFileUrl,
                                        const qulonglong totalBytes, const qlonglong modifiedUtcSeconds)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferManager == nullptr)
    {
        return false;
    }
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    const QString localPath = QUrl(localFileUrl).isLocalFile() ? QUrl(localFileUrl).toLocalFile() : localFileUrl;
    if (!normalized || localPath.trimmed().isEmpty())
    {
        return false;
    }
    const QFileInfo destination(localPath);
    sftp::TransferTask task{
        .id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(QFileInfo(utf8QString(*normalized)).fileName()),
        .sourcePath = *normalized,
        .destinationPath = utf8String(destination.absoluteFilePath()),
        .filenameEncoding = utf8String(tab->sftpFilenameEncoding),
        .direction = sftp::TransferDirection::Download,
        .totalBytes = totalBytes,
        .sourceModifiedUtcSeconds =
            modifiedUtcSeconds >= 0 ? std::optional<std::int64_t>{modifiedUtcSeconds} : std::nullopt,
    };
    const auto queued = m_transferManager->enqueue(std::move(task), transferRequestProvider(tab->sourceProfileId), {});
    return queued.has_value();
}

bool AppController::enqueueSftpUpload(const QString &localFileUrl)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferManager == nullptr)
    {
        return false;
    }
    const QUrl localUrl(localFileUrl);
    const QString localPath = localUrl.isLocalFile() ? localUrl.toLocalFile() : localFileUrl;
    const QFileInfo source(localPath);
    if (source.isDir())
    {
        return enqueueSftpUploadBatch({localFileUrl});
    }
    if (!source.exists() || !source.isFile())
    {
        return false;
    }
    const auto remotePath = sftp::joinRemotePath(utf8String(tab->sftpPath), utf8String(source.fileName()));
    if (!remotePath)
    {
        return false;
    }
    sftp::TransferTask task{
        .id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(source.fileName()),
        .sourcePath = utf8String(source.absoluteFilePath()),
        .destinationPath = *remotePath,
        .filenameEncoding = utf8String(tab->sftpFilenameEncoding),
        .direction = sftp::TransferDirection::Upload,
        .totalBytes = static_cast<std::uint64_t>(source.size()),
        .sourceModifiedUtcSeconds = source.lastModified().toSecsSinceEpoch(),
    };
    const auto queued = m_transferManager->enqueue(std::move(task), transferRequestProvider(tab->sourceProfileId), {});
    return queued.has_value();
}

bool AppController::enqueueSftpUploadBatch(const QStringList &localFileUrls)
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && enqueueSftpUploadBatchForTab(*tab, localFileUrls, tab->sftpPath);
}

bool AppController::enqueueSftpUploadBatchForTab(const TerminalTab &tab, const QStringList &localFileUrls,
                                                 const QString &destinationRoot)
{
    const auto normalizedDestination = sftp::normalizeRemotePath(utf8String(destinationRoot));
    if (tab.kind != TerminalTabKind::Ssh || tab.sourceProfileId.isEmpty() || m_transferBatchCoordinator == nullptr
        || !normalizedDestination || localFileUrls.isEmpty()
        || localFileUrls.size() > static_cast<qsizetype>(sftp::maximumTransferSourceRoots))
    {
        return false;
    }
    std::vector<std::string> roots;
    roots.reserve(static_cast<std::size_t>(localFileUrls.size()));
    for (const QString &value : localFileUrls)
    {
        const QUrl url(value);
        const QString path = url.isLocalFile() ? url.toLocalFile() : value;
        const QFileInfo source(path);
        if (!source.exists() || (!source.isFile() && !source.isDir()))
        {
            return false;
        }
        roots.push_back(utf8String(source.absoluteFilePath()));
    }
    const QString displayName = roots.size() == 1 ? QFileInfo(utf8QString(roots.front())).fileName()
                                                  : tr("Upload %1 items").arg(static_cast<qulonglong>(roots.size()));
    sftp::TransferPlanRequest request{
        .batchId = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab.sourceProfileId),
        .displayName = utf8String(displayName),
        .destinationRoot = *normalizedDestination,
        .sourceRoots = std::move(roots),
        .direction = sftp::TransferBatchDirection::Upload,
    };
    return m_transferBatchCoordinator
        ->enqueue(std::move(request), transferRequestProvider(tab.sourceProfileId),
                  utf8String(tab.sftpFilenameEncoding))
        .has_value();
}

bool AppController::enqueueSftpDownloadBatch(const QStringList &remotePaths, const QString &localDirectoryUrl)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferBatchCoordinator == nullptr || remotePaths.isEmpty()
        || remotePaths.size() > static_cast<qsizetype>(sftp::maximumTransferSourceRoots))
    {
        return false;
    }
    const QUrl destinationUrl(localDirectoryUrl);
    const QString destinationPath = destinationUrl.isLocalFile() ? destinationUrl.toLocalFile() : localDirectoryUrl;
    if (!QFileInfo(destinationPath).isDir())
    {
        return false;
    }
    std::vector<std::string> roots;
    roots.reserve(static_cast<std::size_t>(remotePaths.size()));
    for (const QString &path : remotePaths)
    {
        auto normalized = sftp::normalizeRemotePath(utf8String(path));
        if (!normalized || *normalized == "/")
        {
            return false;
        }
        roots.push_back(std::move(*normalized));
    }
    const QString displayName = roots.size() == 1 ? QFileInfo(utf8QString(roots.front())).fileName()
                                                  : tr("Download %1 items").arg(static_cast<qulonglong>(roots.size()));
    sftp::TransferPlanRequest request{
        .batchId = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(displayName),
        .destinationRoot = utf8String(QFileInfo(destinationPath).absoluteFilePath()),
        .sourceRoots = std::move(roots),
        .direction = sftp::TransferBatchDirection::Download,
    };
    return m_transferBatchCoordinator
        ->enqueue(std::move(request), transferRequestProvider(tab->sourceProfileId),
                  utf8String(tab->sftpFilenameEncoding))
        .has_value();
}

void AppController::cancelTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->cancel(batchId);
    }
}

void AppController::pauseTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->pause(batchId);
    }
}

void AppController::resumeTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->resume(batchId);
    }
}

void AppController::retryTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->retry(batchId);
    }
}

void AppController::dismissTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->dismiss(batchId);
    }
}

void AppController::cancelTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->cancel(taskId);
    }
}

void AppController::pauseTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->pause(taskId);
    }
}

void AppController::resumeTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->resume(taskId);
    }
}

void AppController::retryTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->retry(taskId);
    }
}

void AppController::pauseAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->pauseAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->pauseAll();
    }
}

void AppController::resumeAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->resumeAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->resumeAll();
    }
}

void AppController::cancelAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->cancelAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->cancelAll();
    }
}

bool AppController::copyTransferPath(const QString &taskId)
{
    const auto found = std::ranges::find_if(m_transferTasks, [&taskId](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString() == taskId;
    });
    if (found == m_transferTasks.end())
    {
        return false;
    }
    const QVariantMap task = found->toMap();
    const QString path = task.value(QStringLiteral("destinationPath")).toString();
    if (path.isEmpty())
    {
        return false;
    }
    QGuiApplication::clipboard()->setText(path);
    return true;
}

bool AppController::openTransferTarget(const QString &taskId)
{
    const auto found = std::ranges::find_if(m_transferTasks, [&taskId](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString() == taskId;
    });
    if (found == m_transferTasks.end())
    {
        return false;
    }
    const QVariantMap task = found->toMap();
    if (task.value(QStringLiteral("direction")).toString() != QStringLiteral("download")
        || task.value(QStringLiteral("status")).toString() != QStringLiteral("completed"))
    {
        return false;
    }
    const QFileInfo file(task.value(QStringLiteral("destinationPath")).toString());
    return file.exists() && QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
}

void AppController::dismissTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->dismiss(taskId);
    }
}

void AppController::clearFinishedTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        const QVariantList batches = m_transferBatches;
        for (const QVariant &value : batches)
        {
            const QVariantMap batch = value.toMap();
            const QString status = batch.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("completed") || status == QStringLiteral("failed")
                || status == QStringLiteral("cancelled"))
            {
                m_transferBatchCoordinator->dismiss(batch.value(QStringLiteral("id")).toString());
            }
        }
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->dismissFinished();
    }
}

void AppController::resolveTransferConflict(const QString &taskId, const QString &action,
                                            const QString &renamedDestinationPath, const bool applyToRemainingBatch)
{
    if (m_transferManager == nullptr)
    {
        return;
    }
    const QString token = action.trimmed().toLower();
    const auto decision = token == QLatin1StringView("skip")      ? std::optional{sftp::ConflictAction::Skip}
                          : token == QLatin1StringView("replace") ? std::optional{sftp::ConflictAction::Replace}
                          : token == QLatin1StringView("rename")  ? std::optional{sftp::ConflictAction::Rename}
                          : token == QLatin1StringView("cancel")  ? std::optional{sftp::ConflictAction::Cancel}
                                                                  : std::nullopt;
    if (decision)
    {
        if (applyToRemainingBatch && m_transferBatchCoordinator != nullptr
            && (*decision == sftp::ConflictAction::Skip || *decision == sftp::ConflictAction::Replace))
        {
            m_transferBatchCoordinator->setConflictPolicyForChild(utf8String(taskId),
                                                                  *decision == sftp::ConflictAction::Skip
                                                                      ? sftp::TransferConflictPolicy::Skip
                                                                      : sftp::TransferConflictPolicy::Replace,
                                                                  true);
        }
        m_transferManager->resolveConflict(taskId, *decision, renamedDestinationPath);
    }
}

bool AppController::triggerAction(const QString &actionId)
{
    if (!m_actionRegistry.enabled(actionId, activeTab() != nullptr))
    {
        return false;
    }
    emit actionRequested(actionId);
    return true;
}

QVariantMap AppController::setActionShortcut(const QString &actionId, const QString &shortcut)
{
    const QMap<QString, QString> previousOverrides = m_actionRegistry.overrides();
    const actions::ShortcutValidation validation = m_actionRegistry.setShortcut(actionId, shortcut);
    if (!validation.valid())
    {
        return shortcutResult(validation);
    }

    config::ApplicationSettings updated = m_settings;
    updated.shortcutOverrides = m_actionRegistry.overrides();
    if (!m_settingsStore.save(updated))
    {
        m_actionRegistry.setOverrides(previousOverrides);
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("shortcut"), QString{}},
            {QStringLiteral("error"), tr("The shortcut could not be saved.")},
            {QStringLiteral("conflictingActionId"), QString{}},
        };
    }

    m_settings = std::move(updated);
    emit actionRegistryChanged();
    return shortcutResult(validation);
}

QVariantMap AppController::setActionShortcutFromKey(const QString &actionId, const int key, const int modifiers)
{
    return setActionShortcut(actionId, actions::ActionRegistry::shortcutFromKeyEvent(key, modifiers));
}

bool AppController::resetActionShortcut(const QString &actionId)
{
    if (!m_actionRegistry.contains(actionId))
    {
        return false;
    }
    return setActionShortcut(actionId, m_actionRegistry.defaultShortcut(actionId))
        .value(QStringLiteral("valid"))
        .toBool();
}

bool AppController::resetAllActionShortcuts()
{
    if (m_actionRegistry.overrides().isEmpty())
    {
        return true;
    }
    const QMap<QString, QString> previousOverrides = m_actionRegistry.overrides();
    static_cast<void>(m_actionRegistry.resetAllShortcuts());
    config::ApplicationSettings updated = m_settings;
    updated.shortcutOverrides.clear();
    if (!m_settingsStore.save(updated))
    {
        m_actionRegistry.setOverrides(previousOverrides);
        return false;
    }
    m_settings = std::move(updated);
    emit actionRegistryChanged();
    return true;
}

void AppController::applyTerminalHistoryTaskResult(const QString &tabId, const quint64 requestId,
                                                   ShellHistoryEntries entries, const QString &error)
{
    TerminalTab *target = findTab(tabId);
    if (target == nullptr || target->historyRequestId != requestId)
    {
        return;
    }
    if (!error.isEmpty())
    {
        target->history.clear();
        target->historyState = QStringLiteral("error");
        target->historyError = error;
    }
    else
    {
        target->history = std::move(entries);
        target->historyState = QStringLiteral("ready");
        target->historyError.clear();
    }
    emit terminalHistoryChanged();
}

void AppController::applyNoteSearchTaskResult(const quint64 requestId, const NoteSearchResults &results,
                                              const QString &error)
{
    if (requestId != m_noteSearchRequestId)
    {
        return;
    }
    m_noteSearchResults.clear();
    if (!error.isEmpty())
    {
        m_noteSearchState = QStringLiteral("error");
        m_noteOperationError = error;
    }
    else
    {
        m_noteSearchState = QStringLiteral("ready");
        m_noteOperationError.clear();
        m_noteSearchResults.reserve(static_cast<qsizetype>(results.size()));
        for (const workbench::NoteSearchResult &result : results)
        {
            m_noteSearchResults.append(QVariantMap{{QStringLiteral("path"), result.path},
                                                   {QStringLiteral("title"), result.title},
                                                   {QStringLiteral("snippet"), result.snippet}});
        }
    }
    emit notesChanged();
}

void AppController::applyAiNoteReadTaskResult(const QString &tabId, const quint64 requestId, const quint64 generation,
                                              const QString &relativePath, const QByteArray &outputJson)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || !tab->pendingAiNoteRead.has_value() || !aiTurnActive(*tab)
        || tab->pendingAiNoteRead->requestId != requestId
        || tab->pendingAiNoteRead->request.target.sessionGeneration != generation
        || tab->pendingAiNoteRead->request.relativePath != relativePath)
    {
        return;
    }
    const auto pending = std::move(*tab->pendingAiNoteRead);
    tab->pendingAiNoteRead.reset();
    std::string output = outputJson.toStdString();
    if (tab->reconnectGeneration != generation)
    {
        output = ai::AiNoteReadTool::failure("scope_changed",
                                             "The requested terminal session or generation is no longer active.");
    }
    const QString resultCode = aiActivityResultCode(output);
    recordAiActivity(*tab, pending.call,
                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded") : QStringLiteral("failed"),
                     resultCode, false);
    static_cast<void>(completePendingAiTool(
        *tab, ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = std::move(output)}));
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath, const QString &passphrase)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || privateKeyPath.trimmed().isEmpty() || port <= 0
        || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and private-key fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = privateKeyPath.trimmed(),
        .secret = security::SensitiveByteArray(passphrase.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::connectPassword(const QString &host, const int port, const QString &username,
                                    const QString &password)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || password.isEmpty() || port <= 0 || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and password fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::Password,
        .privateKeyPath = {},
        .secret = security::SensitiveByteArray(password.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::startSshConnection(ssh::SshConnectionRequest request, QString sourceProfileId)
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return false;
    }

    const std::string previousActiveWorkspaceId = m_workspaceState.activeTerminalWorkspaceId;
    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = tab->id;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString fallbackTitle = QStringLiteral("%1@%2").arg(request.username, request.host);
    const std::string profileId = utf8String(sourceProfileId.trimmed());
    const auto sourceProfile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    const QString profileName =
        sourceProfile == m_profiles.end() ? QString{} : utf8QString(sourceProfile->name).trimmed();
    tab->title = profileName.isEmpty() ? fallbackTitle : profileName;
    tab->identity = QStringLiteral("%1@%2:%3").arg(request.username, request.host).arg(request.port);
    tab->address = request.host;
    tab->status = tr("Starting SSH connection...");
    tab->kind = TerminalTabKind::Ssh;
    tab->sourceProfileId = std::move(sourceProfileId);
    if (sourceProfile != m_profiles.end())
    {
        tab->keywordHighlightRules = sourceProfile->keywordHighlightRules;
        tab->keywordHighlightEnabled = sourceProfile->keywordHighlightEnabled;
    }
    applyWorkspaceState(*tab);
    tab->sshPhase = ssh::SshConnectionPhase::Resolving;
    tab->ssh = std::make_unique<ssh::SshTerminalSession>();
    const QString tabId = tab->id;
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    auto workspace = workbench::makeSinglePaneTerminalWorkspace(
        utf8String(tab->workspaceId), utf8String(tab->paneId),
        {.id = utf8String(tab->id),
         .profileId = utf8String(tab->sourceProfileId),
         .title = utf8String(tab->title),
         .kind = tab->sourceProfileId.isEmpty() ? workbench::TerminalRestoreKind::Transient
                                                : workbench::TerminalRestoreKind::SshProfile});
    workspace.title = utf8String(tab->title);
    connectSshTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    m_workspaceState.terminalWorkspaces.push_back(std::move(workspace));
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tabId);
    if (!persistTerminalWorkspaces())
    {
        m_workspaceState.terminalWorkspaces.pop_back();
        m_workspaceState.activeTerminalWorkspaceId = previousActiveWorkspaceId;
        m_tabs.pop_back();
        return false;
    }
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->ssh)
    {
        return false;
    }
    const std::error_code error = created->ssh->start(std::move(request), {.columns = 100, .rows = 30});
    if (error)
    {
        static_cast<void>(closeTerminalTabInternal(tabId, false));
        return false;
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return true;
}

void AppController::scheduleSshReconnect(TerminalTab &tab, const ssh::SshFailureKind failure)
{
    if (tab.reconnectPending || tab.sourceProfileId.isEmpty())
    {
        return;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(tab.sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !ssh::shouldReconnectAfter(profile->sessionOptions.reconnectPolicy, failure))
    {
        return;
    }

    constexpr qint64 stableConnectionMilliseconds = 30'000;
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (tab.connectedUtcMs > 0 && now - tab.connectedUtcMs >= stableConnectionMilliseconds)
    {
        tab.reconnectAttempt = 0;
    }
    if (tab.reconnectAttempt >= profile->sessionOptions.reconnectMaximumAttempts)
    {
        tab.status = tr("Automatic reconnect stopped after %1 attempt(s).")
                         .arg(static_cast<int>(profile->sessionOptions.reconnectMaximumAttempts));
        emit terminalTabsChanged();
        return;
    }

    ++tab.reconnectAttempt;
    m_aiActionToolDispatcher.clearSession(
        {.sessionId = utf8String(tab.id), .sessionGeneration = tab.reconnectGeneration});
    ++tab.reconnectGeneration;
    tab.reconnectPending = true;
    const std::uint32_t delay = ssh::reconnectBackoffMilliseconds(profile->sessionOptions, tab.reconnectAttempt);
    tab.status = tr("SSH connection lost. Reconnecting in %1 second(s) (attempt %2 of %3).")
                     .arg((delay + 999U) / 1000U)
                     .arg(static_cast<int>(tab.reconnectAttempt))
                     .arg(static_cast<int>(profile->sessionOptions.reconnectMaximumAttempts));
    const QString tabId = tab.id;
    const std::uint64_t generation = tab.reconnectGeneration;
    QTimer::singleShot(static_cast<int>(delay), this, [this, tabId, generation] {
        attemptSshReconnect(tabId, generation);
    });
    emit terminalTabsChanged();
}

void AppController::attemptSshReconnect(const QString &tabId, const std::uint64_t generation)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->ssh == nullptr || !tab->reconnectPending
        || tab->reconnectGeneration != generation)
    {
        return;
    }
    tab->reconnectPending = false;
    const auto profile = std::ranges::find(m_profiles, utf8String(tab->sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        tab->status = tr("SSH reconnect stopped because the saved host no longer exists.");
        emit terminalTabsChanged();
        return;
    }
    auto request = connectionRequestForProfile(*profile);
    if (!request)
    {
        tab->status = tr("SSH reconnect needs an available saved credential.");
        emit terminalTabsChanged();
        return;
    }

    tab->sshFailure.reset();
    tab->sshPhase = ssh::SshConnectionPhase::Resolving;
    tab->status = tr("Reconnecting to SSH host...");
    emit terminalTabsChanged();
    const std::error_code error = tab->ssh->start(std::move(*request), {.columns = 100, .rows = 30});
    if (error)
    {
        tab->sshPhase = ssh::SshConnectionPhase::Failed;
        tab->sshFailure = ssh::SshFailureKind::ProtocolError;
        tab->status = tr("SSH reconnect could not start.");
        emit terminalTabsChanged();
        return;
    }
    if (const TerminalTab *started = findTab(tabId); started != nullptr)
    {
        if (ui::TerminalItem *terminal = m_terminalViewports.value(started->paneId))
        {
            terminal->requestCurrentSize();
        }
    }
}

bool AppController::reconnectTerminalTab(const QString &id)
{
    TerminalTab *tab = findTab(id);
    if (tab == nullptr)
    {
        if (const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(id); workspace != nullptr)
        {
            tab = findTabForPane(utf8QString(workspace->activePaneId));
        }
    }
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->ssh == nullptr || tab->running
        || tab->sourceProfileId.isEmpty())
    {
        return false;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(tab->sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    m_aiActionToolDispatcher.clearSession(
        {.sessionId = utf8String(tab->id), .sessionGeneration = tab->reconnectGeneration});
    ++tab->reconnectGeneration;
    tab->reconnectAttempt = 0;
    tab->reconnectPending = true;
    attemptSshReconnect(tab->id, tab->reconnectGeneration);
    return true;
}

bool AppController::cancelTerminalReconnect(const QString &id)
{
    TerminalTab *tab = findTab(id);
    if (tab == nullptr)
    {
        if (const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(id); workspace != nullptr)
        {
            tab = findTabForPane(utf8QString(workspace->activePaneId));
        }
    }
    if (tab == nullptr || !tab->reconnectPending)
    {
        return false;
    }
    tab->reconnectPending = false;
    m_aiActionToolDispatcher.clearSession(
        {.sessionId = utf8String(tab->id), .sessionGeneration = tab->reconnectGeneration});
    ++tab->reconnectGeneration;
    tab->status = tr("Automatic SSH reconnect cancelled.");
    emit terminalTabsChanged();
    return true;
}

std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError>
AppController::sftpConnectionRequest(const TerminalTab &tab)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    const std::string profileId = utf8String(tab.sourceProfileId);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }

    security::SensitiveByteArray secret;
    if (profile->credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile->credentialReference, .kind = credentialKind(*profile)});
        if (!stored)
        {
            return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                       ? sftp::TransferCredentialError::Locked
                                       : sftp::TransferCredentialError::Unavailable);
        }
        secret = std::move(*stored);
    }
    if ((profile->authentication == ssh::SshAuthenticationMethod::Password || profile->privateKeyPassphraseRequired)
        && secret.empty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    security::SensitiveByteArray proxySecret;
    if (profile->proxy.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
        if (!stored)
        {
            return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                       ? sftp::TransferCredentialError::Locked
                                       : sftp::TransferCredentialError::Unavailable);
        }
        proxySecret = std::move(*stored);
    }
    if (!profile->proxy.username.empty() && proxySecret.empty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    auto jumpHosts = storedJumpHostRequests(*profile, m_profiles, m_credentialVaults->active());
    if (!jumpHosts)
    {
        return std::unexpected(jumpHosts.error());
    }
    return ssh::SshConnectionRequest{
        .host = utf8QString(profile->host),
        .port = profile->port,
        .username = utf8QString(profile->username),
        .authentication = profile->authentication,
        .privateKeyPath = utf8QString(profile->privateKeyPath),
        .secret = std::move(secret),
        .proxy = profile->proxy,
        .proxySecret = std::move(proxySecret),
        .jumpHosts = std::move(*jumpHosts),
        .knownHostsPath = m_knownHostsPath,
        .sessionOptions = profile->sessionOptions,
    };
}

sftp::TransferRequestProvider AppController::transferRequestProvider(const QString &profileId)
{
    const std::string id = utf8String(profileId);
    const auto found = std::ranges::find(m_profiles, id, &ssh::SshProfile::id);
    if (found == m_profiles.end() || m_credentialVaults == nullptr)
    {
        return []() -> std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError> {
            return std::unexpected(sftp::TransferCredentialError::Unavailable);
        };
    }
    ssh::SshProfile profile = *found;
    std::vector<ssh::SshProfile> profiles = m_profiles;
    security::CredentialVault *vault = &m_credentialVaults->active();
    QString knownHostsPath = m_knownHostsPath;
    return [profile = std::move(profile), profiles = std::move(profiles), vault,
            knownHostsPath = std::move(
                knownHostsPath)]() noexcept -> std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError> {
        try
        {
            security::SensitiveByteArray secret;
            if (profile.credentialReference)
            {
                auto stored = vault->read({.profileId = *profile.credentialReference, .kind = credentialKind(profile)});
                if (!stored)
                {
                    return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                               ? sftp::TransferCredentialError::Locked
                                               : sftp::TransferCredentialError::Unavailable);
                }
                secret = std::move(*stored);
            }
            if ((profile.authentication == ssh::SshAuthenticationMethod::Password
                 || profile.privateKeyPassphraseRequired)
                && secret.empty())
            {
                return std::unexpected(sftp::TransferCredentialError::Unavailable);
            }
            security::SensitiveByteArray proxySecret;
            if (profile.proxy.credentialReference)
            {
                auto stored = vault->read(
                    {.profileId = *profile.proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
                if (!stored)
                {
                    return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                               ? sftp::TransferCredentialError::Locked
                                               : sftp::TransferCredentialError::Unavailable);
                }
                proxySecret = std::move(*stored);
            }
            if (!profile.proxy.username.empty() && proxySecret.empty())
            {
                return std::unexpected(sftp::TransferCredentialError::Unavailable);
            }
            auto jumpHosts = storedJumpHostRequests(profile, profiles, *vault);
            if (!jumpHosts)
            {
                return std::unexpected(jumpHosts.error());
            }
            return ssh::SshConnectionRequest{
                .host = utf8QString(profile.host),
                .port = profile.port,
                .username = utf8QString(profile.username),
                .authentication = profile.authentication,
                .privateKeyPath = utf8QString(profile.privateKeyPath),
                .secret = std::move(secret),
                .proxy = profile.proxy,
                .proxySecret = std::move(proxySecret),
                .jumpHosts = std::move(*jumpHosts),
                .knownHostsPath = knownHostsPath,
                .sessionOptions = profile.sessionOptions,
            };
        }
        catch (...)
        {
            return std::unexpected(sftp::TransferCredentialError::Unavailable);
        }
    };
}

void AppController::initializeTransferManager()
{
    m_transferManager = std::make_unique<sftp::TransferManager>();
    QObject::connect(m_transferManager.get(), &sftp::TransferManager::tasksChanged, this,
                     &AppController::applyTransferSnapshot);
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::conflictRequired, this,
        [this](const QString &taskId, const sftp::FileConflictPtr &conflict) {
            if (!conflict)
            {
                return;
            }
            if (m_transferBatchCoordinator != nullptr)
            {
                const auto policy = m_transferBatchCoordinator->automaticConflictPolicy(utf8String(taskId));
                if (policy == sftp::TransferConflictPolicy::Skip || policy == sftp::TransferConflictPolicy::Replace)
                {
                    m_transferManager->resolveConflict(taskId, policy == sftp::TransferConflictPolicy::Skip
                                                                   ? sftp::ConflictAction::Skip
                                                                   : sftp::ConflictAction::Replace);
                    return;
                }
            }
            emit transferConflictRequested(
                taskId,
                QVariantMap{
                    {QStringLiteral("sourcePath"), utf8QString(conflict->sourcePath)},
                    {QStringLiteral("destinationPath"), utf8QString(conflict->destinationPath)},
                    {QStringLiteral("destinationExists"), true},
                    {QStringLiteral("destinationDirectory"), conflict->destinationType == sftp::EntryType::Directory},
                    {QStringLiteral("sourceSize"), QVariant::fromValue<qulonglong>(conflict->sourceSize)},
                    {QStringLiteral("destinationSize"), QVariant::fromValue<qulonglong>(conflict->destinationSize)},
                    {QStringLiteral("batchChild"),
                     m_transferBatchCoordinator != nullptr
                         && m_transferBatchCoordinator->ownsChildTask(utf8String(taskId))},
                });
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::hostKeyConfirmationRequired, this,
        [this](const QString &taskId, const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
            if (m_hostKeyPromptVisible)
            {
                m_transferManager->rejectHostKey(taskId);
                return;
            }
            m_hostKeyTransferTaskId = taskId;
            m_hostKeyTabId.clear();
            m_hostKeyForSftp = false;
            m_hostKeyChangedWarning = false;
            m_hostKeyEndpoint = endpoint;
            m_hostKeyAlgorithm = algorithm;
            m_hostKeyFingerprint = fingerprint;
            m_hostKeyPromptVisible = true;
            emit hostKeyPromptChanged();
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::hostKeyChanged, this,
        [this](const QString &taskId, const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
            if (m_hostKeyPromptVisible)
            {
                return;
            }
            m_hostKeyTransferTaskId = taskId;
            m_hostKeyTabId.clear();
            m_hostKeyForSftp = false;
            m_hostKeyChangedWarning = true;
            m_hostKeyEndpoint = endpoint;
            m_hostKeyAlgorithm = algorithm;
            m_hostKeyFingerprint = fingerprint;
            m_hostKeyPromptVisible = true;
            emit hostKeyPromptChanged();
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::recoveryError, this, [this](const QString &errorCode) {
            emit transferNotificationRequested({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("title"), tr("Transfer recovery unavailable")},
                {QStringLiteral("message"),
                 errorCode == QStringLiteral("recovery-load-failed")
                     ? tr("The previous transfer state could not be read. New transfers are still available.")
                     : tr("Transfer recovery state could not be saved. Active transfers may not be recoverable after "
                          "exit.")},
            });
        });
    m_transferManager->enableRecovery(siblingTransferRecoveryFile(m_settingsStore.filePath()),
                                      [this](const std::string &endpointId) {
                                          return transferRequestProvider(utf8QString(endpointId));
                                      });
    m_transferBatchCoordinator = std::make_unique<sftp::TransferBatchCoordinator>(*m_transferManager);
    QObject::connect(m_transferBatchCoordinator.get(), &sftp::TransferBatchCoordinator::batchesChanged, this,
                     &AppController::applyTransferBatchSnapshot);
    QObject::connect(
        m_transferBatchCoordinator.get(), &sftp::TransferBatchCoordinator::recoveryError, this,
        [this](const QString &errorCode) {
            emit transferNotificationRequested({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("title"), tr("Batch transfer recovery unavailable")},
                {QStringLiteral("message"),
                 errorCode == QStringLiteral("batch-recovery-load-failed")
                     ? tr("Previous batch transfer state could not be read. New transfers remain available.")
                     : tr("Batch transfer state could not be saved. Active batches may not be recoverable "
                          "after exit.")},
            });
        });
    m_transferBatchCoordinator->enableRecovery(siblingTransferBatchRecoveryFile(m_settingsStore.filePath()));
}

void AppController::applyTransferSnapshot(const sftp::TransferTasksPtr &tasks)
{
    QHash<QString, QString> previousStatuses;
    previousStatuses.reserve(m_transferTasks.size());
    for (const QVariant &value : std::as_const(m_transferTasks))
    {
        const QVariantMap task = value.toMap();
        previousStatuses.insert(task.value(QStringLiteral("id")).toString(),
                                task.value(QStringLiteral("status")).toString());
    }

    QVariantList values;
    if (tasks)
    {
        values.reserve(static_cast<qsizetype>(tasks->size()));
        for (const sftp::TransferTask &task : *tasks)
        {
            const QVariantMap value = transferTaskValue(task);
            values.push_back(value);

            const QString taskId = value.value(QStringLiteral("id")).toString();
            const QString status = value.value(QStringLiteral("status")).toString();
            const QString previousStatus = previousStatuses.value(taskId);
            const bool terminalStatus = status == QStringLiteral("completed") || status == QStringLiteral("failed")
                                        || status == QStringLiteral("cancelled");
            if (!previousStatus.isEmpty() && previousStatus != status && terminalStatus)
            {
                const bool download = value.value(QStringLiteral("direction")).toString() == QStringLiteral("download");
                QString kind = QStringLiteral("info");
                QString title;
                if (status == QStringLiteral("completed"))
                {
                    kind = QStringLiteral("success");
                    title = download ? tr("Download complete") : tr("Upload complete");
                }
                else if (status == QStringLiteral("failed"))
                {
                    kind = QStringLiteral("error");
                    title = tr("File transfer failed");
                }
                else
                {
                    title = tr("File transfer cancelled");
                }
                emit transferNotificationRequested({
                    {QStringLiteral("taskId"), taskId},
                    {QStringLiteral("kind"), kind},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("message"), value.value(QStringLiteral("displayName")).toString()},
                });
            }
        }
    }
    m_transferTasks = std::move(values);
    emit transferTasksChanged();
}

void AppController::applyTransferBatchSnapshot(const sftp::TransferBatchesPtr &batches)
{
    QVariantList values;
    if (batches)
    {
        values.reserve(static_cast<qsizetype>(batches->size()));
        for (const sftp::TransferBatch &batch : *batches)
        {
            values.push_back(transferBatchValue(batch));
        }
    }
    m_transferBatches = std::move(values);
    emit transferTasksChanged();
}

bool AppController::startSftpSession(TerminalTab &tab)
{
    if (tab.sftpSession != nullptr)
    {
        return true;
    }
    tab.sftpModel = std::make_unique<sftp::SftpDirectoryModel>();
    tab.sftpModel->setShowHidden(m_settings.sftpShowHiddenFiles);
    tab.sftpModel->setViewMode(tab.sftpViewMode);
    tab.sftpModel->setSortColumn(tab.sftpSortColumn);
    tab.sftpModel->setSortAscending(tab.sftpSortAscending);
    tab.sftpModel->setDirectoriesFirst(tab.sftpDirectoriesFirst);
    const QString tabId = tab.id;
    QObject::connect(tab.sftpModel.get(), &sftp::SftpDirectoryModel::treeDirectoryRequested, this,
                     [this, tabId](const QString &remotePath) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpSession == nullptr)
                         {
                             return;
                         }
                         updated->sftpSession->requestTreeDirectory(++updated->sftpTreeRequestId,
                                                                    updated->sftpGeneration, remotePath);
                     });
    auto request = sftpConnectionRequest(tab);
    if (!request)
    {
        tab.sftpState = QStringLiteral("error");
        tab.sftpError = request.error() == sftp::TransferCredentialError::Locked
                            ? tr("Unlock the credential vault to browse remote files.")
                            : tr("This SSH session needs a saved credential before remote files can be opened.");
        emit sftpChanged();
        return false;
    }

    tab.sftpSession = std::make_unique<sftp::SftpSession>();
    tab.sftpSession->setFilenameEncoding(tab.sftpFilenameEncoding);
    connectSftpTabSignals(tab);
    tab.sftpState = QStringLiteral("connecting");
    tab.sftpError.clear();
    emit sftpChanged();
    const std::error_code error = tab.sftpSession->start(std::move(*request));
    if (error)
    {
        tab.sftpSession.reset();
        tab.sftpState = QStringLiteral("error");
        tab.sftpError = tr("The SFTP session could not be started.");
        emit sftpChanged();
        return false;
    }
    return true;
}

void AppController::stopSftpSession(TerminalTab &tab)
{
    if (tab.sftpSession != nullptr)
    {
        deferSftpSessionStop(std::move(tab.sftpSession));
    }
    tab.sftpModel.reset();
    tab.sftpState = QStringLiteral("idle");
    tab.sftpError.clear();
    tab.sftpHomePath.clear();
    tab.sftpHasListing = false;
    ++tab.sftpGeneration;
}

void AppController::deferSftpSessionStop(std::unique_ptr<sftp::SftpSession> session)
{
    if (session == nullptr)
    {
        return;
    }

    sftp::SftpSession *sessionPointer = session.get();
    QObject::connect(sessionPointer, &sftp::SftpSession::workerFinishedChanged, this, [this, sessionPointer] {
        reapStoppedSftpSession(sessionPointer);
    });
    m_stoppingSftpSessions.push_back(std::move(session));
    sessionPointer->requestStop();
    if (sessionPointer->workerFinished())
    {
        reapStoppedSftpSession(sessionPointer);
    }
}

void AppController::reapStoppedSftpSession(sftp::SftpSession *session)
{
    QTimer::singleShot(0, this, [this, session] {
        const auto position =
            std::ranges::find(m_stoppingSftpSessions, session, [](const std::unique_ptr<sftp::SftpSession> &candidate) {
                return candidate.get();
            });
        if (position != m_stoppingSftpSessions.end() && (*position)->workerFinished())
        {
            m_stoppingSftpSessions.erase(position);
        }
    });
}

void AppController::requestSftpDirectory(TerminalTab &tab, const QString &remotePath)
{
    if (tab.sftpSession == nullptr)
    {
        return;
    }
    tab.sftpRequestedPath = remotePath;
    tab.sftpState = QStringLiteral("loading");
    tab.sftpError.clear();
    const std::uint64_t requestId = ++tab.sftpRequestId;
    const std::uint64_t generation = ++tab.sftpGeneration;
    emit sftpChanged();
    tab.sftpSession->requestDirectory(requestId, generation, remotePath);
}

bool AppController::saveHostProfile(const QString &id, const QString &name, const QString &host, const int port,
                                    const QString &username, const QString &authentication,
                                    const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                    const QString &group)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, {}, false, false);
}

bool AppController::saveHostProfileWithCredential(const QString &id, const QString &name, const QString &host,
                                                  const int port, const QString &username,
                                                  const QString &authentication, const QString &privateKeyPath,
                                                  const bool privateKeyPassphraseRequired, const QString &group,
                                                  const QString &secret, const bool rememberCredential,
                                                  const QVariantMap &sessionOptions, const QVariantMap &proxyOptions,
                                                  const QString &proxySecret, const bool rememberProxyCredential,
                                                  const QVariantMap &routeOptions)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, secret, rememberCredential, true,
                                   sessionOptions, proxyOptions, proxySecret, rememberProxyCredential, true,
                                   routeOptions);
}

bool AppController::saveAndConnectHostProfile(const QString &id, const QString &name, const QString &host,
                                              const int port, const QString &username, const QString &authentication,
                                              const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                              const QString &group, const QString &secret,
                                              const bool rememberCredential, const QVariantMap &sessionOptions,
                                              const QVariantMap &proxyOptions, const QString &proxySecret,
                                              const bool rememberProxyCredential, const QVariantMap &routeOptions)
{
    const QString profileId =
        id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    if (!saveHostProfileInternal(profileId, name, host, port, username, authentication, privateKeyPath,
                                 privateKeyPassphraseRequired, group, secret, rememberCredential, true, sessionOptions,
                                 proxyOptions, proxySecret, rememberProxyCredential, true, routeOptions))
    {
        return false;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(profileId), &ssh::SshProfile::id);
    if (profile != m_profiles.end())
    {
        auto request = connectionRequestForProfile(*profile, rememberCredential ? QString{} : secret,
                                                   rememberProxyCredential ? QString{} : proxySecret);
        if (request && startSshConnection(std::move(*request), profileId))
        {
            return true;
        }
    }
    if (m_credentialOperationError.isEmpty())
    {
        setCredentialOperationError(tr("The profile was saved, but the connection could not be started."));
    }
    return false;
}

bool AppController::saveHostProfileInternal(const QString &id, const QString &name, const QString &host, const int port,
                                            const QString &username, const QString &authentication,
                                            const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                            const QString &group, const QString &secret, const bool rememberCredential,
                                            const bool manageCredential, const QVariantMap &sessionOptions,
                                            const QVariantMap &proxyOptions, const QString &proxySecret,
                                            const bool rememberProxyCredential, const bool manageProxyCredential,
                                            const QVariantMap &routeOptions)
{
    const QString normalizedHost = host.trimmed();
    const QString normalizedName = name.trimmed().isEmpty() ? normalizedHost : name.trimmed();
    const QString normalizedGroup = group.trimmed();
    const QString normalizedUsername = username.trimmed();
    const QString normalizedPrivateKeyPath = privateKeyPath.trimmed();
    const QString profileId = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    const std::optional<ssh::SshAuthenticationMethod> authenticationMethod = parseAuthenticationToken(authentication);
    if (!authenticationMethod)
    {
        return false;
    }
    const bool supportsCredential = *authenticationMethod != ssh::SshAuthenticationMethod::Agent;
    const bool effectiveRememberCredential = rememberCredential && supportsCredential;

    const std::string storedProfileId = utf8String(profileId);
    const auto storedProfile = std::ranges::find(m_profiles, storedProfileId, &ssh::SshProfile::id);
    ssh::SshSessionOptions resolvedSessionOptions =
        storedProfile == m_profiles.end() ? ssh::SshSessionOptions{} : storedProfile->sessionOptions;
    if (!sessionOptions.isEmpty())
    {
        auto merged = mergeSessionOptions(sessionOptions, std::move(resolvedSessionOptions));
        if (!merged)
        {
            return false;
        }
        resolvedSessionOptions = std::move(*merged);
    }
    ssh::SshProxyOptions resolvedProxy =
        storedProfile == m_profiles.end() ? ssh::SshProxyOptions{} : storedProfile->proxy;
    if (!proxyOptions.isEmpty())
    {
        auto merged = mergeProxyOptions(proxyOptions, std::move(resolvedProxy));
        if (!merged)
        {
            return false;
        }
        resolvedProxy = std::move(*merged);
    }
    auto resolvedJumpProfileIds = mergeJumpProfileIds(
        routeOptions, storedProfile == m_profiles.end() ? std::vector<std::string>{} : storedProfile->jumpProfileIds,
        storedProfileId, m_profiles);
    if (!resolvedJumpProfileIds)
    {
        setCredentialOperationError(tr("Select up to three distinct saved jump hosts."));
        return false;
    }

    ssh::SshProfile profile{
        .id = storedProfileId,
        .name = utf8String(normalizedName),
        .group = utf8String(normalizedGroup),
        .host = utf8String(normalizedHost),
        .port = port > 0 && port <= 65535 ? static_cast<std::uint16_t>(port) : std::uint16_t{0},
        .username = utf8String(normalizedUsername),
        .authentication = *authenticationMethod,
        .privateKeyPath = *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey
                              ? utf8String(normalizedPrivateKeyPath)
                              : std::string{},
        .privateKeyPassphraseRequired =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired,
        .sessionOptions = std::move(resolvedSessionOptions),
        .proxy = std::move(resolvedProxy),
        .jumpProfileIds = std::move(*resolvedJumpProfileIds),
    };
    if (!ssh::validSshProfile(profile))
    {
        return false;
    }

    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto existing = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    std::optional<security::CredentialKey> previousKey;
    std::optional<security::CredentialKey> previousProxyKey;
    if (existing == updated.end())
    {
        updated.push_back(profile);
    }
    else
    {
        profile.lastConnectedUtcMs = existing->lastConnectedUtcMs;
        if (existing->credentialReference)
        {
            previousKey =
                security::CredentialKey{.profileId = *existing->credentialReference, .kind = credentialKind(*existing)};
        }
        if (existing->proxy.credentialReference)
        {
            previousProxyKey = security::CredentialKey{.profileId = *existing->proxy.credentialReference,
                                                       .kind = security::CredentialKind::ProxyPassword};
        }
        if ((!manageCredential && existing->authentication == profile.authentication)
            || (effectiveRememberCredential && secret.isEmpty() && existing->authentication == profile.authentication))
        {
            profile.credentialReference = existing->credentialReference;
        }
        const bool sameProxyIdentity =
            existing->proxy.type == profile.proxy.type && existing->proxy.host == profile.proxy.host
            && existing->proxy.port == profile.proxy.port && existing->proxy.username == profile.proxy.username;
        if (!sameProxyIdentity)
        {
            profile.proxy.credentialReference.reset();
        }
        else if (!manageProxyCredential
                 || (rememberProxyCredential && proxySecret.isEmpty() && !profile.proxy.username.empty()))
        {
            profile.proxy.credentialReference = existing->proxy.credentialReference;
        }
        *existing = profile;
    }

    const security::CredentialKey desiredKey{.profileId = profile.id, .kind = credentialKind(profile)};
    const bool shouldStore = manageCredential && effectiveRememberCredential && !secret.isEmpty();
    const bool effectiveRememberProxyCredential =
        rememberProxyCredential && profile.proxy.type != ssh::SshProxyType::None && !profile.proxy.username.empty();
    const security::CredentialKey desiredProxyKey{.profileId = profile.id,
                                                  .kind = security::CredentialKind::ProxyPassword};
    const bool shouldStoreProxy = manageProxyCredential && effectiveRememberProxyCredential && !proxySecret.isEmpty();
    if ((shouldStore || shouldStoreProxy) && m_credentialVaults->storage() == security::CredentialStorage::Portable
        && !m_credentialVaults->portableInitialized())
    {
        setCredentialOperationError(
            tr("Create the portable credential vault in Settings > Security before saving a secret."));
        return false;
    }
    const bool shouldRemovePrevious = manageCredential && previousKey
                                      && (!effectiveRememberCredential || (shouldStore && *previousKey != desiredKey)
                                          || (effectiveRememberCredential && secret.isEmpty()
                                              && profile.credentialReference != previousKey->profileId));
    const bool shouldRemovePreviousProxy =
        manageProxyCredential && previousProxyKey
        && (!effectiveRememberProxyCredential || (shouldStoreProxy && *previousProxyKey != desiredProxyKey)
            || (effectiveRememberProxyCredential && proxySecret.isEmpty()
                && profile.proxy.credentialReference != previousProxyKey->profileId));
    if (shouldStore)
    {
        profile.credentialReference = profile.id;
    }
    else if (manageCredential && !effectiveRememberCredential)
    {
        profile.credentialReference.reset();
    }
    if (shouldStoreProxy)
    {
        profile.proxy.credentialReference = profile.id;
    }
    else if (manageProxyCredential && !effectiveRememberProxyCredential)
    {
        profile.proxy.credentialReference.reset();
    }
    const auto target = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    Q_ASSERT(target != updated.end());
    *target = profile;

    CredentialMutation targetCredentialMutation(m_credentialVaults->active());
    auto targetMutation = targetCredentialMutation.apply(desiredKey, previousKey, shouldStore, shouldRemovePrevious,
                                                         shouldStore ? security::SensitiveByteArray(secret.toUtf8())
                                                                     : security::SensitiveByteArray{});
    if (!targetMutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(targetMutation.error()));
        return false;
    }
    CredentialMutation proxyCredentialMutation(m_credentialVaults->active());
    auto proxyMutation = proxyCredentialMutation.apply(
        desiredProxyKey, previousProxyKey, shouldStoreProxy, shouldRemovePreviousProxy,
        shouldStoreProxy ? security::SensitiveByteArray(proxySecret.toUtf8()) : security::SensitiveByteArray{});
    if (!proxyMutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(proxyMutation.error()));
        return false;
    }

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles";
        return false;
    }
    targetCredentialMutation.commit();
    proxyCredentialMutation.commit();
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::duplicateHostProfile(const QString &id)
{
    const auto source = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (source == m_profiles.end())
    {
        return false;
    }

    const QString baseName = utf8QString(source->name) + QStringLiteral(" copy");
    QString copyName = baseName;
    for (int suffix = 2;
         std::ranges::any_of(m_profiles,
                             [&copyName](const ssh::SshProfile &profile) {
                                 return utf8QString(profile.name).compare(copyName, Qt::CaseInsensitive) == 0;
                             });
         ++suffix)
    {
        copyName = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    }

    ssh::SshProfile copy = *source;
    copy.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    copy.name = utf8String(copyName);
    copy.credentialReference.reset();
    copy.proxy.credentialReference.reset();
    copy.lastConnectedUtcMs.reset();
    std::vector<ssh::SshProfile> updated = m_profiles;
    updated.push_back(std::move(copy));
    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist duplicated SSH profile";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::deleteHostProfile(const QString &id)
{
    const std::string profileId = utf8String(id);
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, profileId, &ssh::SshProfile::id);
    if (profile == updated.end())
    {
        return false;
    }
    const auto dependent = std::ranges::find_if(updated, [&profileId](const ssh::SshProfile &candidate) {
        return std::ranges::find(candidate.jumpProfileIds, profileId) != candidate.jumpProfileIds.end();
    });
    if (dependent != updated.end())
    {
        setCredentialOperationError(tr("Remove this host from the jump-host chain of \"%1\" before deleting it.")
                                        .arg(utf8QString(dependent->name)));
        return false;
    }
    if (forwarding::portForwardingRulesReferenceProfile(m_portForwardingRules, profileId))
    {
        setCredentialOperationError(tr("Delete the port forwarding rules that use this host before deleting it."));
        return false;
    }
    const std::optional<security::CredentialKey> key =
        profile->credentialReference ? std::optional{security::CredentialKey{.profileId = *profile->credentialReference,
                                                                             .kind = credentialKind(*profile)}}
                                     : std::nullopt;
    const std::optional<security::CredentialKey> proxyKey =
        profile->proxy.credentialReference
            ? std::optional{security::CredentialKey{.profileId = *profile->proxy.credentialReference,
                                                    .kind = security::CredentialKind::ProxyPassword}}
            : std::nullopt;
    CredentialMutation credentialMutation(m_credentialVaults->active());
    const security::CredentialKey unusedTargetKey{.profileId = profile->id, .kind = credentialKind(*profile)};
    auto mutation = credentialMutation.apply(unusedTargetKey, key, false, key.has_value(), {});
    if (!mutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(mutation.error()));
        return false;
    }
    CredentialMutation proxyCredentialMutation(m_credentialVaults->active());
    const security::CredentialKey unusedProxyKey{.profileId = profile->id,
                                                 .kind = security::CredentialKind::ProxyPassword};
    mutation = proxyCredentialMutation.apply(unusedProxyKey, proxyKey, false, proxyKey.has_value(), {});
    if (!mutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(mutation.error()));
        return false;
    }
    updated.erase(profile);

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles after deletion";
        return false;
    }
    credentialMutation.commit();
    proxyCredentialMutation.commit();
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::clearRecentHostProfiles()
{
    std::vector<ssh::SshProfile> updated = m_profiles;
    bool changed = false;
    for (ssh::SshProfile &profile : updated)
    {
        changed = profile.lastConnectedUtcMs.has_value() || changed;
        profile.lastConnectedUtcMs.reset();
    }
    if (!changed)
    {
        return true;
    }
    if (!m_profileStore.save(updated))
    {
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::setHostSectionCollapsed(const QString &sectionId, const bool collapsed)
{
    const std::string section = utf8String(sectionId.trimmed());
    if (section.empty() || section.size() > 512 || section.find('\0') != std::string::npos)
    {
        return false;
    }

    workbench::WorkspaceState candidate = m_workspaceState;
    const auto existing = std::ranges::find(candidate.collapsedHostSections, section);
    if (collapsed)
    {
        if (existing != candidate.collapsedHostSections.end())
        {
            return true;
        }
        candidate.collapsedHostSections.push_back(section);
    }
    else
    {
        if (existing == candidate.collapsedHostSections.end())
        {
            return true;
        }
        candidate.collapsedHostSections.erase(existing);
    }
    if (!saveWorkspaceStateCandidate(candidate))
    {
        return false;
    }
    m_workspaceState = std::move(candidate);
    emit hostWorkspaceChanged();
    return true;
}

bool AppController::forgetHostCredential(const QString &id)
{
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == updated.end() || !profile->credentialReference)
    {
        return false;
    }
    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto current = m_credentialVaults->active().read(key);
    std::optional<security::SensitiveByteArray> removedSecret;
    if (current)
    {
        removedSecret = std::move(*current);
    }
    else if (current.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(current.error()));
        return false;
    }
    const auto removed = m_credentialVaults->active().remove(key);
    if (!removed && removed.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    profile->credentialReference.reset();
    if (!m_profileStore.save(updated))
    {
        if (removedSecret)
        {
            const std::string_view bytes = removedSecret->view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "forgotten credential restore");
        }
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::saveHostCredential(const QString &id, const QString &secret)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || profile->authentication == ssh::SshAuthenticationMethod::Agent
        || secret.isEmpty())
    {
        return false;
    }
    return saveHostProfileWithCredential(id, utf8QString(profile->name), utf8QString(profile->host), profile->port,
                                         utf8QString(profile->username), authenticationToken(profile->authentication),
                                         utf8QString(profile->privateKeyPath), profile->privateKeyPassphraseRequired,
                                         utf8QString(profile->group), secret, true);
}

bool AppController::saveProxyCredential(const QString &id, const QString &secret)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || profile->proxy.type == ssh::SshProxyType::None || profile->proxy.username.empty()
        || secret.isEmpty())
    {
        return false;
    }
    return saveHostProfileWithCredential(id, utf8QString(profile->name), utf8QString(profile->host), profile->port,
                                         utf8QString(profile->username), authenticationToken(profile->authentication),
                                         utf8QString(profile->privateKeyPath), profile->privateKeyPassphraseRequired,
                                         utf8QString(profile->group), {}, profile->credentialReference.has_value(),
                                         sessionOptionsVariantMap(profile->sessionOptions),
                                         proxyOptionsVariantMap(profile->proxy, false), secret, true);
}

QString AppController::readHostCredential(const QString &id)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !profile->credentialReference)
    {
        setCredentialOperationError(tr("This host profile has no saved credential."));
        return {};
    }

    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto secret = m_credentialVaults->active().read(key);
    if (!secret)
    {
        setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
        return {};
    }

    const std::string_view bytes = secret->view();
    QString result = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    setCredentialOperationError({});
    return result;
}

QString AppController::readProxyCredential(const QString &id)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !profile->proxy.credentialReference)
    {
        setCredentialOperationError(tr("This host profile has no saved proxy credential."));
        return {};
    }

    auto secret = m_credentialVaults->active().read(
        {.profileId = *profile->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
    if (!secret)
    {
        setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
        return {};
    }

    const std::string_view bytes = secret->view();
    QString result = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    setCredentialOperationError({});
    return result;
}

std::optional<ssh::SshConnectionRequest> AppController::connectionRequestForProfile(const ssh::SshProfile &profile,
                                                                                    const QString &secret,
                                                                                    const QString &proxySecret)
{
    security::SensitiveByteArray connectionSecret = profile.authentication == ssh::SshAuthenticationMethod::Agent
                                                        ? security::SensitiveByteArray{}
                                                        : security::SensitiveByteArray(secret.toUtf8());
    if (connectionSecret.empty() && profile.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile.credentialReference, .kind = credentialKind(profile)});
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return std::nullopt;
        }
        connectionSecret = std::move(*stored);
    }
    if ((profile.authentication == ssh::SshAuthenticationMethod::Password || profile.privateKeyPassphraseRequired)
        && connectionSecret.empty())
    {
        setCredentialOperationError(tr("Enter the credential required by this host."));
        return std::nullopt;
    }
    security::SensitiveByteArray connectionProxySecret(proxySecret.toUtf8());
    if (!profile.proxy.username.empty() && connectionProxySecret.empty() && profile.proxy.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile.proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return std::nullopt;
        }
        connectionProxySecret = std::move(*stored);
    }
    if (!profile.proxy.username.empty() && connectionProxySecret.empty())
    {
        setCredentialOperationError(tr("Enter the credential required by this proxy."));
        return std::nullopt;
    }
    std::vector<ssh::SshJumpHostRequest> jumpHosts;
    jumpHosts.reserve(profile.jumpProfileIds.size());
    for (const std::string &jumpProfileId : profile.jumpProfileIds)
    {
        const auto jump = std::ranges::find(m_profiles, jumpProfileId, &ssh::SshProfile::id);
        if (jump == m_profiles.end())
        {
            setCredentialOperationError(tr("A configured jump host no longer exists."));
            return std::nullopt;
        }

        security::SensitiveByteArray jumpSecret;
        if (jump->credentialReference)
        {
            auto stored = m_credentialVaults->active().read(
                {.profileId = *jump->credentialReference, .kind = credentialKind(*jump)});
            if (!stored)
            {
                setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
                return std::nullopt;
            }
            jumpSecret = std::move(*stored);
        }
        if (profileRequiresCredential(*jump) && jumpSecret.empty())
        {
            setCredentialOperationError(
                tr("Save the credential for jump host \"%1\" before using this chain.").arg(utf8QString(jump->name)));
            return std::nullopt;
        }

        security::SensitiveByteArray jumpProxySecret;
        if (jump->proxy.credentialReference)
        {
            auto stored = m_credentialVaults->active().read(
                {.profileId = *jump->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
            if (!stored)
            {
                setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
                return std::nullopt;
            }
            jumpProxySecret = std::move(*stored);
        }
        if (proxyRequiresCredential(*jump) && jumpProxySecret.empty())
        {
            setCredentialOperationError(tr("Save the proxy credential for jump host \"%1\" before using this chain.")
                                            .arg(utf8QString(jump->name)));
            return std::nullopt;
        }

        jumpHosts.push_back({
            .profileId = utf8QString(jump->id),
            .displayName = utf8QString(jump->name),
            .host = utf8QString(jump->host),
            .port = jump->port,
            .username = utf8QString(jump->username),
            .authentication = jump->authentication,
            .privateKeyPath = utf8QString(jump->privateKeyPath),
            .secret = std::move(jumpSecret),
            .proxy = jump->proxy,
            .proxySecret = std::move(jumpProxySecret),
            .connectionTimeoutSeconds = jump->sessionOptions.connectionTimeoutSeconds,
        });
    }
    return ssh::SshConnectionRequest{
        .host = utf8QString(profile.host),
        .port = profile.port,
        .username = utf8QString(profile.username),
        .authentication = profile.authentication,
        .privateKeyPath = utf8QString(profile.privateKeyPath),
        .secret = std::move(connectionSecret),
        .proxy = profile.proxy,
        .proxySecret = std::move(connectionProxySecret),
        .jumpHosts = std::move(jumpHosts),
        .knownHostsPath = m_knownHostsPath,
        .sessionOptions = profile.sessionOptions,
    };
}

bool AppController::connectHostProfile(const QString &id, const QString &secret, const QString &proxySecret)
{
    const std::string profileId = utf8String(id);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    auto request = connectionRequestForProfile(*profile, secret, proxySecret);
    if (!request)
    {
        return false;
    }
    setCredentialOperationError({});
    return startSshConnection(std::move(*request), id);
}

QVariantMap AppController::parseQuickConnectTarget(const QString &target) const
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    if (!parsed)
    {
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("error"), quickConnectError(parsed.error())},
        };
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("error"), QString{}},
        {QStringLiteral("username"), utf8QString(parsed->username)},
        {QStringLiteral("host"), utf8QString(parsed->host)},
        {QStringLiteral("port"), parsed->port},
    };
}

bool AppController::connectQuick(const QString &target, const QString &authentication, const QString &privateKeyPath,
                                 const bool privateKeyPassphraseRequired, const QString &secret,
                                 const bool shouldSaveProfile, const QString &profileName, const QString &group)
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    const auto authenticationMethod = parseAuthenticationToken(authentication);
    if (!parsed || !authenticationMethod)
    {
        return false;
    }
    const QString host = utf8QString(parsed->host);
    const QString username = utf8QString(parsed->username);
    const QString normalizedKeyPath = privateKeyPath.trimmed();
    if ((*authenticationMethod == ssh::SshAuthenticationMethod::Password && secret.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && normalizedKeyPath.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired
            && secret.isEmpty()))
    {
        return false;
    }

    if (shouldSaveProfile)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!saveHostProfileWithCredential(id, profileName, host, parsed->port, username, authentication,
                                           normalizedKeyPath, privateKeyPassphraseRequired, group, secret, true))
        {
            return false;
        }
        return connectHostProfile(id, {});
    }

    ssh::SshConnectionRequest request{
        .host = host,
        .port = parsed->port,
        .username = username,
        .authentication = *authenticationMethod,
        .privateKeyPath =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey ? normalizedKeyPath : QString{},
        .secret = *authenticationMethod == ssh::SshAuthenticationMethod::Agent
                      ? security::SensitiveByteArray{}
                      : security::SensitiveByteArray(secret.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::saveApplicationSettings(
    const QString &theme, const qreal backdropOpacity, const QString &backdrop, const QString &accent,
    const QString &customAccent, const QString &uiFontFamily, const QString &fontFamily, const int fontSize,
    const bool showAllFonts, const bool ligatures, const qreal terminalBackgroundOpacity, const QString &cursor,
    const bool cursorShouldBlink, const bool shouldCopyOnSelect, const bool shouldKeepSelectionAfterCopy,
    const bool shouldConfirmMultilinePaste, const QString &language, const bool shouldShowHiddenSftpFiles,
    const bool shouldConfirmSftpDelete, const bool shouldCloseToTray, const bool shouldPreferPerformance,
    const QString &terminalRightClickBehavior, const QString &terminalMiddleClickBehavior,
    const QString &terminalWordDelimiters, const int terminalScrollRows)
{
    const auto parsedTheme = config::parseThemePreference(theme);
    const auto parsedBackdrop = config::parseBackdropPreference(backdrop);
    const auto parsedAccent = config::parseAccentPreference(accent);
    const auto parsedCursor = config::parseCursorPreference(cursor);
    const auto parsedLanguage = config::parseLanguagePreference(language);
    const auto parsedRightClick = terminalRightClickBehavior.isEmpty()
                                      ? std::optional{m_settings.terminalRightClick}
                                      : config::parseTerminalRightClickPreference(terminalRightClickBehavior);
    const auto parsedMiddleClick = terminalMiddleClickBehavior.isEmpty()
                                       ? std::optional{m_settings.terminalMiddleClick}
                                       : config::parseTerminalMiddleClickPreference(terminalMiddleClickBehavior);
    if (!parsedTheme || !parsedBackdrop || !parsedAccent || !parsedCursor || !parsedLanguage || !parsedRightClick
        || !parsedMiddleClick)
    {
        return false;
    }

    return persistApplicationSettings({
        .backdropOpacity = backdropOpacity,
        .terminalBackgroundOpacity = terminalBackgroundOpacity,
        .shortcutOverrides = m_settings.shortcutOverrides,
        .customAccent = customAccent.trimmed().toUpper(),
        .uiFontFamily = uiFontFamily,
        .terminalFontFamily = fontFamily,
        .terminalWordDelimiters = terminalWordDelimiters,
        .aiBaseUrl = m_settings.aiBaseUrl,
        .aiEndpointPath = m_settings.aiEndpointPath,
        .aiModel = m_settings.aiModel,
        .aiCredentialReference = m_settings.aiCredentialReference,
        .aiProxyUrl = m_settings.aiProxyUrl,
        .aiProxyUsername = m_settings.aiProxyUsername,
        .terminalFontSize = fontSize,
        .terminalScrollRows = terminalScrollRows,
        .theme = *parsedTheme,
        .backdrop = *parsedBackdrop,
        .accent = *parsedAccent,
        .showAllTerminalFonts = showAllFonts,
        .terminalLigatures = ligatures,
        .cursor = *parsedCursor,
        .cursorBlink = cursorShouldBlink,
        .copyOnSelect = shouldCopyOnSelect,
        .keepSelectionAfterCopy = shouldKeepSelectionAfterCopy,
        .confirmMultilinePaste = shouldConfirmMultilinePaste,
        .terminalRightClick = *parsedRightClick,
        .terminalMiddleClick = *parsedMiddleClick,
        .sftpShowHiddenFiles = shouldShowHiddenSftpFiles,
        .sftpConfirmDelete = shouldConfirmSftpDelete,
        .closeToTray = shouldCloseToTray,
        .performanceMode = shouldPreferPerformance,
        .credentialStorage = m_settings.credentialStorage,
        .language = *parsedLanguage,
        .aiProvider = m_settings.aiProvider,
        .aiAutomaticContext = m_settings.aiAutomaticContext,
        .aiPermission = m_settings.aiPermission,
        .aiConversationHistoryEnabled = m_settings.aiConversationHistoryEnabled,
        .aiDebugTraceEnabled = m_settings.aiDebugTraceEnabled,
        .aiReasoning = m_settings.aiReasoning,
        .aiProxy = m_settings.aiProxy,
    });
}

bool AppController::saveAiProviderSettings(const QString &provider, const QString &baseUrl, const QString &endpointPath,
                                           const QString &model, const bool automaticContext,
                                           const QString &permissionMode)
{
    const auto parsedProvider = config::parseAiProviderPreference(provider);
    const auto parsedPermission = config::parseAiPermissionPreference(permissionMode);
    if (!parsedProvider || !parsedPermission)
    {
        return false;
    }
    const bool providerChanged = *parsedProvider != m_settings.aiProvider;
    auto candidate = m_settings;
    candidate.aiProvider = *parsedProvider;
    candidate.aiBaseUrl = baseUrl.trimmed();
    candidate.aiEndpointPath = endpointPath.trimmed();
    candidate.aiModel = model.trimmed();
    candidate.aiAutomaticContext = automaticContext;
    candidate.aiPermission = *parsedPermission;
    if (!persistApplicationSettings(candidate))
    {
        return false;
    }
    if (providerChanged)
    {
        resetAiModelCatalog();
    }
    return true;
}

bool AppController::saveAiProviderConfiguration(const QString &provider, const QString &baseUrl, const QString &model,
                                                const bool automaticContext, const QString &permissionMode,
                                                const QString &apiKey, const bool debugTraceEnabled,
                                                const QString &reasoningPreference)
{
    const auto parsedProvider = config::parseAiProviderPreference(provider);
    const auto parsedPermission = config::parseAiPermissionPreference(permissionMode);
    const auto parsedReasoning = config::parseAiReasoningPreference(reasoningPreference);
    if (!parsedProvider || !parsedPermission || !parsedReasoning)
    {
        return false;
    }
    const QStringList supportedReasoning = supportedAiReasoningTokens(*parsedProvider, model);
    if (!supportedReasoning.contains(config::aiReasoningPreferenceToken(*parsedReasoning)))
    {
        return false;
    }
    const bool providerChanged = *parsedProvider != m_settings.aiProvider;
    auto candidate = m_settings;
    candidate.aiProvider = *parsedProvider;
    candidate.aiBaseUrl = baseUrl.trimmed();
    candidate.aiEndpointPath.clear();
    candidate.aiModel = model.trimmed();
    candidate.aiAutomaticContext = automaticContext;
    candidate.aiPermission = *parsedPermission;
    candidate.aiDebugTraceEnabled = debugTraceEnabled;
    candidate.aiReasoning = *parsedReasoning;
    if (*parsedProvider != m_settings.aiProvider || !apiKey.isEmpty())
    {
        candidate.aiCredentialReference = aiCredentialReference(*parsedProvider);
    }
    if (!persistApplicationSettings(candidate))
    {
        return false;
    }
    if (providerChanged)
    {
        resetAiModelCatalog();
    }
    if (!apiKey.isEmpty() && !saveAiApiKey(apiKey))
    {
        return false;
    }
    refreshConfiguredAiModels();
    if (*parsedProvider == config::AiProviderPreference::openAiChatGpt)
    {
        refreshAiChatGptUsageInternal(true);
    }
    return true;
}

QVariantMap AppController::aiReasoningCapabilities(const QString &provider, const QString &model) const
{
    const auto parsedProvider = config::parseAiProviderPreference(provider);
    const QStringList tokens =
        parsedProvider ? supportedAiReasoningTokens(*parsedProvider, model) : QStringList{QStringLiteral("auto")};
    QStringList labels;
    labels.reserve(tokens.size());
    for (const QString &token : tokens)
    {
        if (token == QStringLiteral("off"))
        {
            labels.push_back(tr("Off"));
        }
        else if (token == QStringLiteral("low"))
        {
            labels.push_back(tr("Low"));
        }
        else if (token == QStringLiteral("medium"))
        {
            labels.push_back(tr("Medium"));
        }
        else if (token == QStringLiteral("high"))
        {
            labels.push_back(tr("High"));
        }
        else if (token == QStringLiteral("max"))
        {
            labels.push_back(tr("Maximum"));
        }
        else
        {
            labels.push_back(tr("Automatic"));
        }
    }
    return {{QStringLiteral("tokens"), tokens},
            {QStringLiteral("labels"), labels},
            {QStringLiteral("configurable"), tokens.size() > 1},
            {QStringLiteral("description"),
             tokens.size() > 1
                 ? tr("Uses this provider's documented reasoning controls. Unsupported levels are not shown.")
                 : tr("This provider or model does not expose a documented reasoning control; ztermy leaves it "
                      "unchanged.")}};
}

QString AppController::aiProviderEndpointPreview(const QString &provider, const QString &baseUrl) const
{
    const auto parsedProvider = config::parseAiProviderPreference(provider);
    if (!parsedProvider)
    {
        return tr("Invalid API address");
    }
    if (*parsedProvider == config::AiProviderPreference::openAiChatGpt)
    {
        return ai::OpenAiSubscriptionAuthProtocol::inferenceUrl().toString(QUrl::FullyEncoded);
    }
    const ai::AiProviderConfiguration configuration{.kind = aiProviderKind(*parsedProvider),
                                                    .flavor = aiProviderFlavor(*parsedProvider),
                                                    .baseUrl = utf8String(baseUrl)};
    const auto endpoint = ai::resolveProviderEndpoint(configuration, ai::ProviderEndpointPurpose::generation);
    return endpoint.has_value() ? endpoint->toString(QUrl::FullyEncoded) : tr("Invalid API address");
}

void AppController::refreshAiModels(const QString &provider, const QString &baseUrl, const QString &apiKey)
{
    refreshAiModelsInternal(provider, baseUrl, apiKey, false);
}

void AppController::resetAiModelCatalog()
{
    ++m_aiModelsRequestGeneration;
    if (m_aiModelsReply)
    {
        QNetworkReply *reply = std::exchange(m_aiModelsReply, nullptr);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    const bool changed = m_aiModelsLoading || !m_aiAvailableModels.isEmpty() || !m_aiModelsError.isEmpty();
    m_aiModelsLoading = false;
    m_aiAvailableModels.clear();
    m_aiModelReasoningSummarySupport.clear();
    m_aiModelsError.clear();
    if (changed)
    {
        emit aiModelsChanged();
    }
}

void AppController::withAiChatGptAccessToken(AiSubscriptionAccessHandler handler)
{
    if (!handler)
    {
        return;
    }
    ai::AiSecretStore store(m_credentialVaults->active());
    std::string reference = aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString();
    auto tokens = store.readOAuthTokens(reference);
    if (!tokens.has_value())
    {
        handler(std::unexpected(ai::AiProviderError{.code = ai::AiProviderErrorCode::authentication,
                                                    .message = "Sign in with ChatGPT first.",
                                                    .retryable = false}),
                {});
        return;
    }

    const auto expiration = ai::OpenAiSubscriptionAuthProtocol::expirationUtcSeconds(tokens->accessToken.view());
    constexpr qint64 refreshSkewSeconds = 120;
    if (!expiration.has_value()
        || *expiration > QDateTime::currentDateTimeUtc().toSecsSinceEpoch() + refreshSkewSeconds)
    {
        const QString accountId =
            ai::OpenAiSubscriptionAuthProtocol::accountIdFromAccessToken(tokens->accessToken.view());
        if (accountId.isEmpty())
        {
            handler(std::unexpected(ai::AiProviderError{.code = ai::AiProviderErrorCode::authentication,
                                                        .message = "The ChatGPT account identity is unavailable.",
                                                        .retryable = false}),
                    {});
            return;
        }
        if (m_aiChatGptAccountId != accountId)
        {
            m_aiChatGptAccountId = accountId;
            emit aiSubscriptionAuthChanged();
        }
        handler(std::move(tokens->accessToken), accountId);
        return;
    }

    m_aiSubscriptionAccessHandlers.push_back(std::move(handler));
    if (!m_aiSubscriptionTokenRefresher)
    {
        m_aiSubscriptionTokenRefresher = std::make_unique<ai::OpenAiSubscriptionTokenRefresher>(&m_aiNetwork, this);
    }
    if (m_aiSubscriptionTokenRefresher->active())
    {
        return;
    }
    auto completion = [this, reference = std::move(reference)](ai::OpenAiSubscriptionTokenRefresher::Result result) {
        auto handlers = std::exchange(m_aiSubscriptionAccessHandlers, {});
        if (!result.has_value())
        {
            for (auto &pending : handlers)
            {
                pending(std::unexpected(result.error()), {});
            }
            return;
        }
        ai::AiSecretStore currentStore(m_credentialVaults->active());
        QString accountId = result->accountId;
        if (accountId.isEmpty())
        {
            accountId = ai::OpenAiSubscriptionAuthProtocol::accountIdFromAccessToken(result->accessToken.view());
        }
        if (accountId.isEmpty())
        {
            const ai::AiProviderError error{.code = ai::AiProviderErrorCode::authentication,
                                            .message = "The refreshed ChatGPT account identity is unavailable.",
                                            .retryable = false};
            for (auto &pending : handlers)
            {
                pending(std::unexpected(error), {});
            }
            return;
        }
        auto stored =
            currentStore.storeOAuthTokens(reference, {.accessToken = cloneSensitiveSecret(result->accessToken),
                                                      .refreshToken = std::move(result->refreshToken)});
        if (!stored.has_value())
        {
            const ai::AiProviderError error{.code = ai::AiProviderErrorCode::authentication,
                                            .message = "The refreshed ChatGPT sign-in could not be saved.",
                                            .retryable = false};
            for (auto &pending : handlers)
            {
                pending(std::unexpected(error), {});
            }
            return;
        }
        m_aiChatGptAccountId = accountId;
        m_aiChatGptAuthError.clear();
        emit credentialVaultChanged();
        emit aiSubscriptionAuthChanged();
        for (auto &pending : handlers)
        {
            pending(cloneSensitiveSecret(result->accessToken), accountId);
        }
    };
    auto started = m_aiSubscriptionTokenRefresher->start(std::move(tokens->refreshToken), std::move(completion));
    if (!started.has_value())
    {
        auto handlers = std::exchange(m_aiSubscriptionAccessHandlers, {});
        for (auto &pending : handlers)
        {
            pending(std::unexpected(started.error()), {});
        }
    }
}

void AppController::refreshAiModelsInternal(const QString &provider, const QString &baseUrl, const QString &apiKey,
                                            const bool background)
{
    const auto parsedProvider = config::parseAiProviderPreference(provider);
    if (!parsedProvider)
    {
        if (!background)
        {
            m_aiModelsError = tr("Choose a supported model provider.");
            emit aiModelsChanged();
        }
        return;
    }
    if (background && m_aiModelsReply)
    {
        return;
    }
    ++m_aiModelsRequestGeneration;
    const auto generation = m_aiModelsRequestGeneration;
    if (m_aiModelsReply)
    {
        m_aiModelsReply->abort();
        m_aiModelsReply->deleteLater();
        m_aiModelsReply = nullptr;
    }

    if (*parsedProvider == config::AiProviderPreference::openAiChatGpt)
    {
        if (!background)
        {
            m_aiModelsLoading = true;
            m_aiModelsError.clear();
            emit aiModelsChanged();
        }
        withAiChatGptAccessToken([this, generation, background](auto accessToken, const QString &accountId) mutable {
            if (generation != m_aiModelsRequestGeneration)
            {
                return;
            }
            if (!accessToken.has_value())
            {
                m_aiModelsLoading = false;
                if (!background)
                {
                    m_aiModelsError = QString::fromStdString(accessToken.error().message);
                    emit aiModelsChanged();
                }
                return;
            }
            auto request = ai::ProviderModelCatalog::prepareOpenAiSubscriptionRequest(
                std::move(*accessToken), accountId, QCoreApplication::applicationVersion());
            if (!request.has_value())
            {
                m_aiModelsLoading = false;
                if (!background)
                {
                    m_aiModelsError = QString::fromStdString(request.error().message);
                    emit aiModelsChanged();
                }
                return;
            }
            startAiModelsRequest(*request, generation, ai::AiProviderKind::openAiResponses, background, true);
        });
        return;
    }

    security::SensitiveByteArray secret;
    if (!apiKey.isEmpty())
    {
        secret = security::SensitiveByteArray(apiKey.toUtf8());
    }
    else if (*parsedProvider != config::AiProviderPreference::ollama)
    {
        const ai::AiSecretStore store(m_credentialVaults->active());
        const auto reference = *parsedProvider == m_settings.aiProvider ? m_settings.aiCredentialReference
                                                                        : aiCredentialReference(*parsedProvider);
        auto stored = store.readApiKey(reference.toStdString());
        if (stored.has_value())
        {
            secret = std::move(*stored);
        }
    }
    const ai::AiProviderConfiguration configuration{.kind = aiProviderKind(*parsedProvider),
                                                    .flavor = aiProviderFlavor(*parsedProvider),
                                                    .baseUrl = utf8String(baseUrl)};
    auto request = ai::ProviderModelCatalog::prepareRequest(configuration, std::move(secret));
    if (!request.has_value())
    {
        m_aiModelsLoading = false;
        if (!background)
        {
            m_aiModelsError = QString::fromStdString(request.error().message);
            emit aiModelsChanged();
        }
        return;
    }

    if (!background)
    {
        m_aiModelsLoading = true;
        m_aiModelsError.clear();
        emit aiModelsChanged();
    }
    startAiModelsRequest(*request, generation, configuration.kind, background, false);
}

void AppController::startAiModelsRequest(const QNetworkRequest &request, const quint64 generation,
                                         const ai::AiProviderKind kind, const bool background,
                                         const bool openAiSubscription)
{
    auto *reply = m_aiNetwork.get(request);
    m_aiModelsReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, kind, background, openAiSubscription] {
        if (generation != m_aiModelsRequestGeneration || m_aiModelsReply != reply)
        {
            reply->deleteLater();
            return;
        }
        m_aiModelsReply = nullptr;
        m_aiModelsLoading = false;
        QString error;
        QStringList models;
        QHash<QString, bool> reasoningSummarySupport;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400)
        {
            constexpr qsizetype maximumErrorBytes = qsizetype{64} * 1024;
            QByteArray body = reply->read(maximumErrorBytes + 1);
            if (body.size() > maximumErrorBytes)
            {
                body.truncate(maximumErrorBytes);
            }
            const ai::AiProviderError providerError =
                ai::parseProviderReplyError(*reply, body,
                                            status > 0 ? utf8String(tr("Could not fetch models (HTTP %1).").arg(status))
                                                       : utf8String(tr("Could not reach the model provider.")));
            error = utf8QString(providerError.message);
            if (openAiSubscription && providerError.code == ai::AiProviderErrorCode::authentication)
            {
                m_aiChatGptAuthError = error;
                emit aiSubscriptionAuthChanged();
            }
        }
        else
        {
            constexpr qsizetype maximumCatalogBytes = qsizetype{2} * 1024 * 1024;
            const auto body = reply->read(maximumCatalogBytes + 1);
            if (body.size() > maximumCatalogBytes)
            {
                error = tr("The provider model list is too large.");
            }
            else
            {
                if (openAiSubscription)
                {
                    const auto parsed = ai::ProviderModelCatalog::parseOpenAiSubscription(body);
                    if (parsed.has_value())
                    {
                        models.reserve(parsed->models.size());
                        reasoningSummarySupport.reserve(parsed->models.size());
                        for (const auto &entry : parsed->models)
                        {
                            models.push_back(entry.slug);
                            reasoningSummarySupport.insert(entry.slug, entry.supportsReasoningSummaryParameter);
                        }
                    }
                    else
                    {
                        error = QString::fromStdString(parsed.error().message);
                    }
                }
                else
                {
                    const auto parsed = ai::ProviderModelCatalog::parse(kind, body);
                    if (parsed.has_value())
                    {
                        models = *parsed;
                    }
                    else
                    {
                        error = QString::fromStdString(parsed.error().message);
                    }
                }
                if (error.isEmpty())
                {
                    if (models.isEmpty())
                    {
                        error = tr("The provider returned no models.");
                    }
                }
            }
        }
        if (error.isEmpty())
        {
            m_aiAvailableModels = std::move(models);
            m_aiModelReasoningSummarySupport = std::move(reasoningSummarySupport);
            m_aiModelsError.clear();
            if (openAiSubscription && !m_aiChatGptAuthError.isEmpty())
            {
                m_aiChatGptAuthError.clear();
                emit aiSubscriptionAuthChanged();
            }
        }
        else if (!background)
        {
            m_aiModelsError = error;
        }
        reply->deleteLater();
        if (error.isEmpty() || !background)
        {
            emit aiModelsChanged();
        }
    });
}

bool AppController::saveAiApiKey(const QString &apiKey)
{
    ai::AiSecretStore store(m_credentialVaults->active());
    auto stored = store.storeApiKey(m_settings.aiCredentialReference.toStdString(),
                                    security::SensitiveByteArray(apiKey.toUtf8()));
    if (!stored)
    {
        setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::removeAiApiKey()
{
    ai::AiSecretStore store(m_credentialVaults->active());
    const auto removed = store.removeApiKey(m_settings.aiCredentialReference.toStdString());
    if (!removed && removed.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::saveAiProxySettings(const QString &mode, const QString &url, const QString &username,
                                        const QString &password)
{
    const auto parsedMode = config::parseAiProxyPreference(mode);
    if (!parsedMode.has_value())
    {
        return false;
    }
    auto candidate = m_settings;
    candidate.aiProxy = *parsedMode;
    candidate.aiProxyUrl = url.trimmed();
    candidate.aiProxyUsername = username.trimmed();
    if (!persistApplicationSettings(candidate))
    {
        return false;
    }
    if (!password.isEmpty())
    {
        ai::AiSecretStore store(m_credentialVaults->active());
        auto stored = store.storeApiKey(AiProxyCredentialReference, security::SensitiveByteArray(password.toUtf8()));
        if (!stored.has_value())
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return false;
        }
        setCredentialOperationError({});
        emit credentialVaultChanged();
        applyAiNetworkProxy();
    }
    return true;
}

bool AppController::removeAiProxyPassword()
{
    ai::AiSecretStore store(m_credentialVaults->active());
    const auto removed = store.removeApiKey(AiProxyCredentialReference);
    if (!removed.has_value() && removed.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    applyAiNetworkProxy();
    return true;
}

bool AppController::beginAiChatGptSignIn()
{
    if (!m_aiSubscriptionAuth)
    {
        m_aiSubscriptionAuth = std::make_unique<ai::OpenAiSubscriptionAuthSession>(&m_aiNetwork, this);
    }
    if (m_aiSubscriptionAuth->active())
    {
        return false;
    }
    m_aiChatGptAuthError.clear();
    emit aiSubscriptionAuthChanged();

    m_aiChatGptDeviceVerificationUrl = QUrl{};
    m_aiChatGptDeviceUserCode.clear();
    auto started = m_aiSubscriptionAuth->beginBrowserLogin([this](ai::OpenAiSubscriptionAuthSession::Result result) {
        completeAiChatGptSignIn(std::move(result));
    });
    if (!started)
    {
        m_aiChatGptAuthError = started.error().message;
        emit aiSubscriptionAuthChanged();
        return false;
    }
    if (!QDesktopServices::openUrl(*started))
    {
        m_aiSubscriptionAuth->cancel();
        m_aiChatGptAuthError = tr("Could not open the browser for ChatGPT sign-in.");
        emit aiSubscriptionAuthChanged();
        return false;
    }
    return true;
}

bool AppController::beginAiChatGptDeviceSignIn()
{
    if (!m_aiSubscriptionAuth)
    {
        m_aiSubscriptionAuth = std::make_unique<ai::OpenAiSubscriptionAuthSession>(&m_aiNetwork, this);
    }
    if (m_aiSubscriptionAuth->active())
    {
        return false;
    }
    m_aiChatGptAuthError.clear();
    m_aiChatGptDeviceVerificationUrl = QUrl{};
    m_aiChatGptDeviceUserCode.clear();
    emit aiSubscriptionAuthChanged();
    auto started = m_aiSubscriptionAuth->beginDeviceLogin(
        [this](const ai::OpenAiSubscriptionDeviceCode &deviceCode) {
            m_aiChatGptDeviceVerificationUrl = deviceCode.verificationUrl;
            m_aiChatGptDeviceUserCode = deviceCode.userCode;
            emit aiSubscriptionAuthChanged();
            static_cast<void>(QDesktopServices::openUrl(deviceCode.verificationUrl));
        },
        [this](ai::OpenAiSubscriptionAuthSession::Result result) {
            completeAiChatGptSignIn(std::move(result));
        });
    if (!started)
    {
        m_aiChatGptAuthError = started.error().message;
        emit aiSubscriptionAuthChanged();
        return false;
    }
    return true;
}

void AppController::completeAiChatGptSignIn(ai::OpenAiSubscriptionAuthSession::Result result)
{
    m_aiChatGptDeviceVerificationUrl = QUrl{};
    m_aiChatGptDeviceUserCode.clear();
    if (!result)
    {
        m_aiChatGptAuthError = result.error().code == ai::OpenAiSubscriptionSessionErrorCode::cancelled
                                   ? QString{}
                                   : result.error().message;
        emit aiSubscriptionAuthChanged();
        return;
    }
    ai::AiSecretStore store(m_credentialVaults->active());
    const auto reference = aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString();
    auto stored = store.storeOAuthTokens(
        reference, {.accessToken = std::move(result->accessToken), .refreshToken = std::move(result->refreshToken)});
    if (!stored)
    {
        m_aiChatGptAuthError = credentialVaultErrorMessage(stored.error());
        emit aiSubscriptionAuthChanged();
        return;
    }
    m_aiChatGptAccountId = result->accountId;
    m_aiChatGptAuthError.clear();
    setCredentialOperationError({});
    emit credentialVaultChanged();
    emit aiSubscriptionAuthChanged();
    refreshConfiguredAiModels();
    refreshAiChatGptUsageInternal(true);
}

void AppController::cancelAiChatGptSignIn()
{
    if (m_aiSubscriptionAuth)
    {
        m_aiSubscriptionAuth->cancel();
    }
}

bool AppController::openAiChatGptDeviceVerification()
{
    return m_aiChatGptDeviceVerificationUrl.isValid() && QDesktopServices::openUrl(m_aiChatGptDeviceVerificationUrl);
}

bool AppController::copyAiChatGptDeviceCode()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || m_aiChatGptDeviceUserCode.isEmpty())
    {
        return false;
    }
    clipboard->setText(m_aiChatGptDeviceUserCode);
    return true;
}

bool AppController::signOutAiChatGpt()
{
    if (m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt)
    {
        resetAiModelCatalog();
    }
    cancelAiChatGptSignIn();
    if (m_aiSubscriptionTokenRefresher && m_aiSubscriptionTokenRefresher->active())
    {
        m_aiSubscriptionTokenRefresher->cancel();
    }
    ++m_aiSubscriptionUsageGeneration;
    if (m_aiSubscriptionUsageReply)
    {
        m_aiSubscriptionUsageReply->abort();
        m_aiSubscriptionUsageReply->deleteLater();
        m_aiSubscriptionUsageReply = nullptr;
    }
    ai::AiSecretStore store(m_credentialVaults->active());
    const auto removed =
        store.removeOAuthTokens(aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString());
    if (!removed)
    {
        m_aiChatGptAuthError = credentialVaultErrorMessage(removed.error());
        emit aiSubscriptionAuthChanged();
        return false;
    }
    m_aiChatGptAccountId.clear();
    m_aiChatGptAuthError.clear();
    m_aiChatGptDeviceVerificationUrl = QUrl{};
    m_aiChatGptDeviceUserCode.clear();
    m_aiSubscriptionUsage.reset();
    m_aiSubscriptionUsageError.clear();
    m_aiSubscriptionUsageLoading = false;
    emit credentialVaultChanged();
    emit aiSubscriptionAuthChanged();
    emit aiSubscriptionUsageChanged();
    return true;
}

void AppController::refreshAiChatGptUsage()
{
    refreshAiChatGptUsageInternal(false);
}

void AppController::refreshAiChatGptUsageInternal(const bool background)
{
    if (!aiChatGptConfigured())
    {
        if (!background)
        {
            m_aiSubscriptionUsageError = tr("Sign in with ChatGPT to view subscription usage.");
            emit aiSubscriptionUsageChanged();
        }
        return;
    }
    if (background && m_aiSubscriptionUsageReply)
    {
        return;
    }
    ++m_aiSubscriptionUsageGeneration;
    const quint64 generation = m_aiSubscriptionUsageGeneration;
    if (m_aiSubscriptionUsageReply)
    {
        m_aiSubscriptionUsageReply->abort();
        m_aiSubscriptionUsageReply->deleteLater();
        m_aiSubscriptionUsageReply = nullptr;
    }
    m_aiSubscriptionUsageLoading = true;
    if (!background)
    {
        m_aiSubscriptionUsageError.clear();
    }
    emit aiSubscriptionUsageChanged();
    withAiChatGptAccessToken([this, generation, background](auto accessToken, const QString &accountId) mutable {
        if (generation != m_aiSubscriptionUsageGeneration)
        {
            return;
        }
        if (!accessToken.has_value())
        {
            m_aiSubscriptionUsageLoading = false;
            if (!background)
            {
                m_aiSubscriptionUsageError = QString::fromStdString(accessToken.error().message);
            }
            if (accessToken.error().code == ai::AiProviderErrorCode::authentication)
            {
                m_aiChatGptAuthError = QString::fromStdString(accessToken.error().message);
                emit aiSubscriptionAuthChanged();
            }
            emit aiSubscriptionUsageChanged();
            return;
        }
        auto request = ai::OpenAiSubscriptionUsage::prepareRequest(std::move(*accessToken), accountId,
                                                                   QCoreApplication::applicationVersion());
        if (!request.has_value())
        {
            m_aiSubscriptionUsageLoading = false;
            if (!background)
            {
                m_aiSubscriptionUsageError = QString::fromStdString(request.error().message);
            }
            emit aiSubscriptionUsageChanged();
            return;
        }
        QNetworkReply *reply = m_aiNetwork.get(*request);
        m_aiSubscriptionUsageReply = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, background] {
            if (generation != m_aiSubscriptionUsageGeneration || m_aiSubscriptionUsageReply != reply)
            {
                reply->deleteLater();
                return;
            }
            m_aiSubscriptionUsageReply = nullptr;
            m_aiSubscriptionUsageLoading = false;
            QString error;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status >= 400)
            {
                constexpr qsizetype maximumErrorBytes = qsizetype{64} * 1024;
                QByteArray body = reply->read(maximumErrorBytes + 1);
                if (body.size() > maximumErrorBytes)
                {
                    body.truncate(maximumErrorBytes);
                }
                const ai::AiProviderError providerError = ai::parseProviderReplyError(
                    *reply, body,
                    status == 401 || status == 403 ? std::string("ChatGPT sign-in expired. Sign in again.")
                    : status > 0 ? utf8String(tr("Could not fetch ChatGPT usage (HTTP %1).").arg(status))
                                 : utf8String(tr("Could not reach ChatGPT usage.")));
                error = utf8QString(providerError.message);
                if (providerError.code == ai::AiProviderErrorCode::authentication)
                {
                    m_aiChatGptAuthError = error;
                    emit aiSubscriptionAuthChanged();
                }
            }
            else
            {
                constexpr qsizetype maximumUsageBytes = qsizetype{512} * 1024;
                const QByteArray body = reply->read(maximumUsageBytes + 1);
                if (body.size() > maximumUsageBytes)
                {
                    error = tr("ChatGPT returned an oversized usage response.");
                }
                else
                {
                    auto parsed = ai::OpenAiSubscriptionUsage::parse(body);
                    if (parsed.has_value())
                    {
                        m_aiSubscriptionUsage = std::move(*parsed);
                        m_aiSubscriptionUsageError.clear();
                        if (!m_aiChatGptAuthError.isEmpty())
                        {
                            m_aiChatGptAuthError.clear();
                            emit aiSubscriptionAuthChanged();
                        }
                    }
                    else
                    {
                        error = QString::fromStdString(parsed.error().message);
                    }
                }
            }
            if (!error.isEmpty() && !background)
            {
                m_aiSubscriptionUsageError = std::move(error);
            }
            reply->deleteLater();
            emit aiSubscriptionUsageChanged();
        });
    });
}

bool AppController::saveMcpServer(const QString &id, const QString &nameSpace, const QString &program,
                                  const QStringList &arguments, const QString &workingDirectory, const QString &trust,
                                  const bool enabled)
{
    ai::McpServerTrust parsedTrust = ai::McpServerTrust::disabled;
    if (trust == QStringLiteral("observe"))
    {
        parsedTrust = ai::McpServerTrust::observe;
    }
    else if (trust == QStringLiteral("execute"))
    {
        parsedTrust = ai::McpServerTrust::execute;
    }
    else if (trust != QStringLiteral("disabled"))
    {
        return false;
    }
    return m_mcpRuntime.saveServer(ai::McpServerRecord{
        .configuration = {.identity = {.id = utf8String(id.trimmed()),
                                       .nameSpace = utf8String(nameSpace.trimmed()),
                                       .trust = parsedTrust},
                          .program = QDir::cleanPath(program.trimmed()),
                          .arguments = arguments,
                          .workingDirectory =
                              workingDirectory.isEmpty() ? QString{} : QDir::cleanPath(workingDirectory.trimmed())},
        .enabled = enabled});
}

bool AppController::removeMcpServer(const QString &id)
{
    return m_mcpRuntime.removeServer(utf8String(id.trimmed()));
}

bool AppController::restartMcpServer(const QString &id)
{
    return m_mcpRuntime.restartServer(utf8String(id.trimmed()));
}

bool AppController::setMcpToolApproved(const QString &serverId, const QString &exposedName, const QString &schemaDigest,
                                       const bool approved)
{
    return m_mcpRuntime.setToolApproved(utf8String(serverId), utf8String(exposedName), utf8String(schemaDigest),
                                        approved);
}

bool AppController::setAiConversationHistoryEnabled(const bool enabled)
{
    if (enabled && !m_credentialVaults->active().persistent())
    {
        setCredentialOperationError(
            tr("Encrypted AI history requires Windows Credential Manager or the portable vault."));
        return false;
    }
    if (enabled && m_credentialVaults->storage() == security::CredentialStorage::Portable
        && m_credentialVaults->portableLocked())
    {
        setCredentialOperationError(tr("Unlock the portable credential vault before enabling encrypted AI history."));
        return false;
    }
    auto candidate = m_settings;
    candidate.aiConversationHistoryEnabled = enabled;
    if (!persistApplicationSettings(candidate))
    {
        return false;
    }
    setCredentialOperationError({});
    if (m_aiConversationHistory)
    {
        if (enabled)
        {
            m_aiConversationHistory->reload();
        }
    }
    return true;
}

bool AppController::restoreAiConversationHistory(const QString &conversationId)
{
    TerminalTab *tab = activeTab();
    if (!m_settings.aiConversationHistoryEnabled || tab == nullptr || !tab->aiConversation || aiTurnActive(*tab)
        || !m_aiConversationHistory)
    {
        return false;
    }
    const auto stored = m_aiConversationHistory->conversation(conversationId);
    if (!stored.has_value())
    {
        return false;
    }
    const auto alreadyOpen = std::ranges::any_of(m_tabs, [&](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate && candidate->id != tab->id && candidate->aiConversationId == stored->id;
    });
    if (alreadyOpen)
    {
        return false;
    }
    std::vector<ai::AiConversationTranscriptEntry> transcript;
    transcript.reserve(stored->messages.size());
    for (const auto &message : stored->messages)
    {
        ai::AiConversationTranscriptRole role;
        if (message.role == QStringLiteral("user"))
        {
            role = ai::AiConversationTranscriptRole::user;
        }
        else if (message.role == QStringLiteral("assistant"))
        {
            role = ai::AiConversationTranscriptRole::assistant;
        }
        else if (message.role == QStringLiteral("evidence"))
        {
            role = ai::AiConversationTranscriptRole::evidence;
        }
        else
        {
            return false;
        }
        transcript.push_back({.role = role,
                              .content = utf8String(message.text),
                              .reasoning = utf8String(message.reasoning),
                              .toolActivities = message.toolActivities,
                              .sources = message.sources,
                              .providerReplayJson = message.providerReplayJson,
                              .usage = message.usage,
                              .metrics = message.metrics,
                              .estimatedCostUsd = message.estimatedCostUsd,
                              .costCatalogDate = utf8String(message.costCatalogDate),
                              .longContextRates = message.longContextRates,
                              .truncated = message.truncated,
                              .contextAttachments = message.contextAttachments});
    }
    if (!tab->aiConversation->restoreTranscript(transcript))
    {
        return false;
    }
    tab->aiExplicitContextItems.clear();
    tab->aiImageAttachments.clear();
    tab->aiExcludedContextIds.clear();
    tab->aiPinnedContextIds.clear();
    tab->aiContextItems.clear();
    tab->aiContextPreview.clear();
    tab->aiCompaction.clear();
    m_aiActionToolDispatcher.clearConversation(utf8String(tab->aiConversationId));
    m_aiCommandTracker.clearConversation(utf8String(tab->aiConversationId));
    tab->aiConversationId = stored->id;
    tab->aiState = QStringLiteral("complete");
    tab->aiError.clear();
    tab->aiTurnBudget.reset();
    tab->pendingAiAction.reset();
    emit aiConversationChanged();
    return true;
}

bool AppController::sendAiMessage(const QString &prompt)
{
    TerminalTab *tab = activeTab();
    return tab != nullptr && sendAiMessage(*tab, prompt, false);
}

bool AppController::sendAiCommandRequest(const QString &prompt)
{
    TerminalTab *tab = activeTab();
    return tab != nullptr && sendAiMessage(*tab, prompt, false, true, true);
}

bool AppController::sendAiMessageWithSkills(const QString &prompt, const QStringList &skillIds)
{
    TerminalTab *tab = activeTab();
    return tab != nullptr && sendAiMessage(*tab, prompt, false, true, false, skillIds);
}

bool AppController::sendAiCommandRequestWithSkills(const QString &prompt, const QStringList &skillIds)
{
    TerminalTab *tab = activeTab();
    return tab != nullptr && sendAiMessage(*tab, prompt, false, true, true, skillIds);
}

bool AppController::sendAiPrompt(const QString &prompt, const bool commandRequest, const QStringList &skillIds,
                                 const bool webSearchEnabled)
{
    TerminalTab *tab = activeTab();
    return tab != nullptr && sendAiMessage(*tab, prompt, false, true, commandRequest, skillIds, webSearchEnabled);
}

bool AppController::setAiPermissionMode(const QString &mode)
{
    const auto parsed = config::parseAiPermissionPreference(mode);
    if (!parsed.has_value())
    {
        return false;
    }
    auto candidate = m_settings;
    candidate.aiPermission = *parsed;
    return persistApplicationSettings(candidate);
}

bool AppController::explainAiLastFailure()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->semanticObserver)
    {
        return false;
    }
    const auto snapshot = tab->semanticObserver->snapshot();
    const auto failure = std::ranges::find_if(snapshot.commandBlocks.rbegin(), snapshot.commandBlocks.rend(),
                                              [](const terminal::CommandBlock &block) {
                                                  return block.state == terminal::CommandBlockState::finished
                                                         && block.exitStatus.has_value() && *block.exitStatus != 0;
                                              });
    if (failure == snapshot.commandBlocks.rend() || failure->capability == terminal::TerminalSemanticCapability::none)
    {
        tab->aiError = tr("No command failure with a reliable exit status is available in this session.");
        emit aiConversationChanged();
        return false;
    }
    return sendAiMessage(
        *tab,
        tr("Explain the last failed command, identify the likely cause, and suggest the safest next diagnostic step."),
        true);
}

bool AppController::cancelAiMessage()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !aiTurnActive(*tab))
    {
        return false;
    }
    tab->aiState = QStringLiteral("cancelling");
    emit aiConversationChanged();
    return cancelAiTurn(*tab);
}

bool AppController::applyPendingAiPermissionRule(TerminalTab &tab, const QString &durationToken,
                                                 const QString &matcherToken, const QString &pattern,
                                                 const ai::AiPermissionDisposition disposition)
{
    if (!tab.pendingAiAction.has_value() && !tab.pendingAiMcpCall.has_value())
    {
        return false;
    }
    const auto duration = parseAiPermissionDuration(durationToken);
    if (!duration.has_value())
    {
        return false;
    }
    if (*duration == ai::AiPermissionRuleDuration::once)
    {
        return true;
    }
    const auto matcher = parseAiPermissionMatcher(matcherToken);
    if (!matcher.has_value())
    {
        return false;
    }
    if (*duration == ai::AiPermissionRuleDuration::profile && tab.sourceProfileId.isEmpty())
    {
        m_aiPermissionRuleError = tr("This terminal is not linked to a saved Profile.");
        emit aiPermissionRulesChanged();
        return false;
    }

    const auto capability = tab.pendingAiMcpCall.has_value() ? ai::AiPermissionCapability::mcpTool
                                                             : tab.pendingAiAction->permissionCapability;
    const std::string subject =
        tab.pendingAiMcpCall.has_value() ? tab.pendingAiMcpCall->call.name : tab.pendingAiAction->permissionSubject;
    ai::AiPermissionRule rule{
        .id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .capability = capability,
        .matcher = *matcher,
        .pattern = *matcher == ai::AiPermissionRuleMatcher::all
                       ? std::string{}
                       : utf8String(pattern.isEmpty() ? utf8QString(subject) : pattern),
        .disposition = disposition,
        .duration = *duration,
        .sessionId = *duration == ai::AiPermissionRuleDuration::session ? utf8String(tab.id) : std::string{},
        .profileId =
            *duration == ai::AiPermissionRuleDuration::profile ? utf8String(tab.sourceProfileId) : std::string{},
    };
    std::vector<ai::AiPermissionRule> rules = m_aiActionToolDispatcher.permissionRules();
    rules.push_back(std::move(rule));
    return replaceAndPersistAiPermissionRules(std::move(rules));
}

bool AppController::approveAiToolWithRule(const QString &duration, const QString &matcher, const QString &pattern)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (!tab->pendingAiMcpCall.has_value() && !tab->pendingAiAction.has_value())
        || !applyPendingAiPermissionRule(*tab, duration, matcher, pattern, ai::AiPermissionDisposition::allow))
    {
        return false;
    }
    return approveAiTool();
}

bool AppController::denyAiToolWithRule(const QString &duration, const QString &matcher, const QString &pattern)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (!tab->pendingAiMcpCall.has_value() && !tab->pendingAiAction.has_value())
        || !applyPendingAiPermissionRule(*tab, duration, matcher, pattern, ai::AiPermissionDisposition::deny))
    {
        return false;
    }
    return denyAiTool();
}

bool AppController::updateAiPermissionRule(const QString &id, const QString &matcherToken, const QString &pattern,
                                           const bool enabled)
{
    const auto matcher = parseAiPermissionMatcher(matcherToken);
    if (!matcher.has_value())
    {
        return false;
    }
    std::vector<ai::AiPermissionRule> rules = m_aiActionToolDispatcher.permissionRules();
    const auto target = std::ranges::find_if(rules, [&id](const ai::AiPermissionRule &rule) {
        return rule.id == utf8String(id);
    });
    if (target == rules.end())
    {
        return false;
    }
    target->matcher = *matcher;
    target->pattern = *matcher == ai::AiPermissionRuleMatcher::all ? std::string{} : utf8String(pattern);
    target->enabled = enabled;
    return replaceAndPersistAiPermissionRules(std::move(rules));
}

bool AppController::deleteAiPermissionRule(const QString &id)
{
    std::vector<ai::AiPermissionRule> rules = m_aiActionToolDispatcher.permissionRules();
    const std::string ruleId = utf8String(id);
    const auto previousSize = rules.size();
    std::erase_if(rules, [&ruleId](const ai::AiPermissionRule &rule) {
        return rule.id == ruleId;
    });
    return rules.size() != previousSize && replaceAndPersistAiPermissionRules(std::move(rules));
}

bool AppController::saveAiQuickMessage(const QString &id, const QString &name, const QString &slug,
                                       const QString &content, const QString &description)
{
    const QString normalizedName = name.trimmed();
    const QString normalizedContent = normalizedQuickCommandText(content).trimmed();
    const QString normalizedDescription = description.trimmed();
    const std::string normalizedSlug = ai::normalizeAiQuickMessageSlug(utf8String(slug));
    if (normalizedName.isEmpty() || normalizedContent.isEmpty() || !ai::validAiQuickMessageSlug(normalizedSlug)
        || reservedAiQuickMessageSlug(normalizedSlug))
    {
        setAiQuickMessageError(tr("Enter a name, a unique slash command, and prompt text."));
        return false;
    }

    std::vector<ai::AiQuickMessage> candidate = m_aiQuickMessages;
    const std::int64_t now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    ai::AiQuickMessage message{
        .id = id.isEmpty() ? utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)) : utf8String(id),
        .name = utf8String(normalizedName),
        .slug = normalizedSlug,
        .content = utf8String(normalizedContent),
        .description = utf8String(normalizedDescription),
        .createdUtcMs = now,
        .modifiedUtcMs = now,
    };
    const auto duplicate = std::ranges::find_if(candidate, [&message](const ai::AiQuickMessage &existing) {
        return existing.id != message.id && existing.slug == message.slug;
    });
    if (duplicate != candidate.end())
    {
        setAiQuickMessageError(tr("This slash command is already used."));
        return false;
    }
    if (!ai::validAiQuickMessage(message))
    {
        setAiQuickMessageError(tr("The quick message is too long or contains unsupported characters."));
        return false;
    }
    if (id.isEmpty())
    {
        if (candidate.size() >= static_cast<std::size_t>(ai::maximumAiQuickMessageCount))
        {
            setAiQuickMessageError(tr("The quick message limit has been reached."));
            return false;
        }
        candidate.push_back(std::move(message));
    }
    else
    {
        const auto existing = std::ranges::find(candidate, message.id, &ai::AiQuickMessage::id);
        if (existing == candidate.end())
        {
            setAiQuickMessageError(tr("The quick message no longer exists."));
            return false;
        }
        message.createdUtcMs = existing->createdUtcMs;
        *existing = std::move(message);
    }
    if (!m_aiQuickMessageStore.save(candidate))
    {
        setAiQuickMessageError(tr("The quick message could not be saved."));
        return false;
    }
    m_aiQuickMessages = std::move(candidate);
    m_aiQuickMessageError.clear();
    emit aiQuickMessagesChanged();
    return true;
}

bool AppController::deleteAiQuickMessage(const QString &id)
{
    std::vector<ai::AiQuickMessage> candidate = m_aiQuickMessages;
    const std::string messageId = utf8String(id);
    const auto previousSize = candidate.size();
    std::erase_if(candidate, [&messageId](const ai::AiQuickMessage &message) {
        return message.id == messageId;
    });
    if (candidate.size() == previousSize)
    {
        setAiQuickMessageError(tr("The quick message no longer exists."));
        return false;
    }
    if (!m_aiQuickMessageStore.save(candidate))
    {
        setAiQuickMessageError(tr("The quick message could not be deleted."));
        return false;
    }
    m_aiQuickMessages = std::move(candidate);
    m_aiQuickMessageError.clear();
    emit aiQuickMessagesChanged();
    return true;
}

void AppController::ensureAiUserSkillsLoaded()
{
    if (m_aiUserSkillsState == QStringLiteral("idle"))
    {
        reloadAiUserSkills();
    }
}

void AppController::reloadAiUserSkills()
{
    const quint64 requestGeneration = ++m_aiUserSkillsRequestGeneration;
    m_aiUserSkillsState = QStringLiteral("loading");
    m_aiUserSkillsError.clear();
    emit aiUserSkillsChanged();

    const QString rootPath = m_aiUserSkillCatalog.rootPath();
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, rootPath, requestGeneration] {
        auto scanned = ai::AiUserSkillCatalog(rootPath).scan();
        if (!self)
        {
            return;
        }
        emit self->aiUserSkillTaskCompleted(
            requestGeneration, scanned.has_value() ? std::move(scanned->skills) : AiUserSkills{}, scanned.has_value());
    });
}

void AppController::applyAiUserSkillTaskResult(const quint64 requestGeneration, AiUserSkills skills,
                                               const bool succeeded)
{
    if (requestGeneration != m_aiUserSkillsRequestGeneration)
    {
        return;
    }
    if (!succeeded)
    {
        m_aiUserSkillsState = QStringLiteral("error");
        m_aiUserSkillsError = tr("The user skills folder could not be scanned.");
        m_openAiUserSkillsAfterReload = false;
        emit aiUserSkillsChanged();
        return;
    }
    m_aiUserSkills = std::move(skills);
    m_aiUserSkillsState = QStringLiteral("ready");
    m_aiUserSkillsError.clear();
    if (m_openAiUserSkillsAfterReload)
    {
        m_openAiUserSkillsAfterReload = false;
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_aiUserSkillCatalog.rootPath())))
        {
            m_aiUserSkillsError = tr("The user skills folder could not be opened.");
        }
    }
    emit aiUserSkillsChanged();
}

bool AppController::openAiUserSkillsDirectory()
{
    if (!QDir(m_aiUserSkillCatalog.rootPath()).exists())
    {
        m_openAiUserSkillsAfterReload = true;
        reloadAiUserSkills();
        return true;
    }
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(m_aiUserSkillCatalog.rootPath())))
    {
        return true;
    }
    m_aiUserSkillsError = tr("The user skills folder could not be opened.");
    emit aiUserSkillsChanged();
    return false;
}

bool AppController::approveAiTool()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->pendingAiMcpCall.has_value())
    {
        return approveAiMcpTool(*tab);
    }
    if (tab == nullptr || !aiTurnActive(*tab) || !tab->pendingAiAction.has_value())
    {
        return false;
    }
    const auto pendingCall = pendingAiToolCall(*tab);
    const auto action = *tab->pendingAiAction;
    if (!pendingCall.has_value() || pendingCall->id != action.dispatchKey.toolCallId
        || !m_aiActionToolDispatcher.approve(action))
    {
        return false;
    }
    tab->pendingAiAction.reset();
    recordAiActivity(*tab, *pendingCall, QStringLiteral("executing"), QStringLiteral("pending"), true,
                     action.risk.highRisk());
    std::string output;
    const bool requiresLiveTerminal = action.kind != ai::AiTerminalActionKind::saveRunbook
                                      && action.kind != ai::AiTerminalActionKind::enqueueSftpDownload
                                      && action.kind != ai::AiTerminalActionKind::enqueueSftpUpload;
    if (action.target.sessionId != utf8String(tab->id) || action.target.sessionGeneration != tab->reconnectGeneration
        || (requiresLiveTerminal && (!tab->running || (!tab->ssh && !tab->local))))
    {
        output = aiToolFailureJson(QStringLiteral("scope_changed"),
                                   tr("The target terminal session changed before approval."));
        static_cast<void>(m_aiActionToolDispatcher.complete(action, ai::AiToolDispatchState::failed, output));
    }
    else
    {
        if (action.kind == ai::AiTerminalActionKind::runCommand)
        {
            const auto handled = handleAiRunCommand(*tab, tab->id, *pendingCall, action);
            if (!handled.output.has_value())
            {
                emit aiConversationChanged();
                return true;
            }
            output = handled.output->outputJson;
        }
        else
        {
            output = executeAiTerminalAction(*tab, action);
            static_cast<void>(m_aiActionToolDispatcher.complete(action, ai::AiToolDispatchState::succeeded, output));
        }
    }
    if (action.kind == ai::AiTerminalActionKind::runCommand)
    {
        const bool resumed = completePendingAiTool(
            *tab,
            ai::AiToolOutput{.callId = pendingCall->id, .name = pendingCall->name, .outputJson = std::move(output)});
        if (!resumed)
        {
            tab->aiError = tr("The command result could not resume the AI turn.");
            tab->aiState = QStringLiteral("error");
        }
        emit aiConversationChanged();
        return resumed;
    }
    const QString activityResult = aiActivityResultCode(output);
    recordAiActivity(*tab, *pendingCall,
                     activityResult == QStringLiteral("ok") ? QStringLiteral("succeeded") : QStringLiteral("failed"),
                     activityResult, true, action.risk.highRisk());
    const bool resumed = completePendingAiTool(
        *tab, ai::AiToolOutput{.callId = pendingCall->id, .name = pendingCall->name, .outputJson = std::move(output)});
    if (!resumed)
    {
        tab->aiError = tr("The command result could not resume the AI turn.");
        tab->aiState = QStringLiteral("error");
    }
    emit aiConversationChanged();
    return resumed;
}

bool AppController::denyAiTool()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->pendingAiMcpCall.has_value())
    {
        return denyAiMcpTool(*tab);
    }
    if (tab == nullptr || !aiTurnActive(*tab) || !tab->pendingAiAction.has_value())
    {
        return false;
    }
    const auto pendingCall = pendingAiToolCall(*tab);
    const auto action = *tab->pendingAiAction;
    if (!pendingCall.has_value() || pendingCall->id != action.dispatchKey.toolCallId
        || !m_aiActionToolDispatcher.deny(action))
    {
        return false;
    }
    tab->pendingAiAction.reset();
    const auto output =
        aiToolFailureJson(QStringLiteral("permission_denied"), tr("The user denied this terminal action."));
    recordAiActivity(*tab, *pendingCall, QStringLiteral("cancelled"), QStringLiteral("permission_denied"), true,
                     action.risk.highRisk());
    const bool resumed = completePendingAiTool(
        *tab, ai::AiToolOutput{.callId = pendingCall->id, .name = pendingCall->name, .outputJson = output});
    if (!resumed)
    {
        tab->aiError = tr("The denied command result could not resume the AI turn.");
        tab->aiState = QStringLiteral("error");
    }
    emit aiConversationChanged();
    return resumed;
}

bool AppController::approveAiMcpTool(TerminalTab &tab)
{
    if (!aiTurnActive(tab) || !tab.pendingAiMcpCall.has_value())
    {
        return false;
    }
    const auto runnerCall = pendingAiToolCall(tab);
    const auto pending = *tab.pendingAiMcpCall;
    const auto currentTool = m_mcpRuntime.resolve(pending.call.name);
    const auto ledgerRecord = m_mcpDispatchLedger.find(pending.dispatchKey);
    if (!runnerCall.has_value() || runnerCall->id != pending.call.id || !currentTool.has_value()
        || currentTool->serverId != pending.tool.serverId || currentTool->schemaDigest != pending.tool.schemaDigest
        || !ledgerRecord.has_value() || ledgerRecord->request.sessionId != utf8String(tab.id)
        || ledgerRecord->request.sessionGeneration != tab.reconnectGeneration
        || pending.dispatchKey.turnId != activeAiTurnId(tab)
        || !m_mcpDispatchLedger.transition(pending.dispatchKey, ai::AiToolDispatchState::running))
    {
        return false;
    }
    recordAiActivity(tab, pending.call, QStringLiteral("executing"), QStringLiteral("pending"), true, true);
    const QString tabId = tab.id;
    const std::uint64_t generation = tab.reconnectGeneration;
    auto request = m_mcpRuntime.call(
        pending.call.name, pending.call.argumentsJson,
        [this, tabId, generation, key = pending.dispatchKey, call = pending.call](auto result) mutable {
            completeAiMcpTool(tabId, generation, key, call, std::move(result));
        });
    if (!request.has_value())
    {
        completeAiMcpTool(tabId, generation, pending.dispatchKey, pending.call, std::unexpected(request.error()));
        return true;
    }
    if (tab.pendingAiMcpCall.has_value() && tab.pendingAiMcpCall->dispatchKey == pending.dispatchKey)
    {
        tab.pendingAiMcpCall->activeCall = *request;
    }
    emit aiConversationChanged();
    return true;
}

bool AppController::denyAiMcpTool(TerminalTab &tab)
{
    if (!aiTurnActive(tab) || !tab.pendingAiMcpCall.has_value())
    {
        return false;
    }
    const auto runnerCall = pendingAiToolCall(tab);
    const auto pending = *tab.pendingAiMcpCall;
    if (!runnerCall.has_value() || runnerCall->id != pending.call.id)
    {
        return false;
    }
    const auto output =
        aiToolFailureJson(QStringLiteral("permission_denied"), tr("The user denied this MCP tool call."));
    static_cast<void>(m_mcpDispatchLedger.transition(pending.dispatchKey, ai::AiToolDispatchState::cancelled, output));
    tab.pendingAiMcpCall.reset();
    recordAiActivity(tab, pending.call, QStringLiteral("cancelled"), QStringLiteral("permission_denied"), true, true);
    const bool resumed = completePendingAiTool(
        tab, ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output});
    if (!resumed)
    {
        tab.aiError = tr("The denied MCP tool result could not resume the AI turn.");
        tab.aiState = QStringLiteral("error");
    }
    emit aiConversationChanged();
    return resumed;
}

void AppController::completeAiMcpTool(const QString &tabId, const std::uint64_t generation,
                                      const ai::AiToolDispatchKey &dispatchKey, const ai::AiToolCall &call,
                                      std::expected<std::string, QString> result)
{
    std::string output =
        result.has_value() ? std::move(*result) : aiToolFailureJson(QStringLiteral("mcp_call_failed"), result.error());
    const QString resultCode = aiActivityResultCode(output);
    const auto state = result.has_value() && resultCode == QStringLiteral("ok") ? ai::AiToolDispatchState::succeeded
                                                                                : ai::AiToolDispatchState::failed;
    static_cast<void>(m_mcpDispatchLedger.transition(dispatchKey, state, output));
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || tab->reconnectGeneration != generation || !aiTurnActive(*tab)
        || !tab->pendingAiMcpCall.has_value() || tab->pendingAiMcpCall->dispatchKey != dispatchKey)
    {
        return;
    }
    tab->pendingAiMcpCall.reset();
    recordAiActivity(*tab, call,
                     state == ai::AiToolDispatchState::succeeded ? QStringLiteral("succeeded")
                                                                 : QStringLiteral("failed"),
                     resultCode, true, true);
    const bool resumed = completePendingAiTool(
        *tab, ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = std::move(output)});
    if (!resumed)
    {
        tab->aiError = tr("The MCP tool result could not resume the AI turn.");
        tab->aiState = QStringLiteral("error");
    }
    if (m_activeTabId == tabId || m_focusedTabId == tabId)
    {
        emit aiConversationChanged();
    }
}

bool AppController::retryAiMessage()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (tab->aiState != QStringLiteral("error") && tab->aiState != QStringLiteral("cancelled"))
        || tab->aiLastPrompt.isEmpty() || aiTurnActive(*tab))
    {
        return false;
    }
    return sendAiMessage(*tab, tab->aiLastPrompt, tab->aiLastPreferFailure, false, tab->aiLastCommandRequest,
                         tab->aiLastSelectedSkillIds, tab->aiLastWebSearchEnabled);
}

void AppController::clearAiConversation()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->aiConversation || aiTurnActive(*tab))
    {
        return;
    }
    const QString previousConversationId = tab->aiConversationId;
    tab->aiConversation->clear();
    tab->aiAssistantMessageId = 0;
    tab->aiState = QStringLiteral("idle");
    tab->aiError.clear();
    tab->aiProviderErrorCode.reset();
    tab->aiProviderRetryAfterMilliseconds.reset();
    tab->aiLastPrompt.clear();
    tab->aiLastPreferFailure = false;
    tab->aiLastCommandRequest = false;
    tab->aiLastWebSearchEnabled = false;
    tab->aiWebSearchQueries.clear();
    tab->aiContextPreview.clear();
    tab->aiContextItems.clear();
    tab->aiCompaction.clear();
    tab->aiExplicitContextItems.clear();
    tab->aiImageAttachments.clear();
    tab->aiExcludedContextIds.clear();
    tab->aiPinnedContextIds.clear();
    tab->aiTurnBudget.reset();
    tab->pendingAiAction.reset();
    tab->pendingAiMcpCall.reset();
    m_aiActionToolDispatcher.clearConversation(utf8String(previousConversationId));
    m_mcpDispatchLedger.clearConversation(utf8String(previousConversationId));
    m_aiCommandTracker.clearConversation(utf8String(previousConversationId));
    tab->aiConversationId = tab->id + QLatin1Char(':') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit aiConversationChanged();
}

void AppController::clearAiActivity()
{
    m_aiActivity.clear();
}

bool AppController::exportAiActivity(const QString &localFileUrl) const
{
    const QUrl url(localFileUrl);
    return m_aiActivity.exportTo(url.isLocalFile() ? url.toLocalFile() : localFileUrl);
}

bool AppController::exportAiConversation(const QString &localFileUrl) const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->aiConversation)
    {
        return false;
    }
    const auto messages = tab->aiConversation->providerMessages();
    if (messages.empty())
    {
        return false;
    }

    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    QByteArray markdown("# ztermy AI conversation\n\n");
    for (const auto &message : messages)
    {
        const auto heading = message.role == ai::AiMessageRole::user ? QByteArrayView("## User\n\n")
                                                                     : QByteArrayView("## Assistant\n\n");
        markdown.append(heading);
        markdown.append(message.content);
        if (!markdown.endsWith('\n'))
        {
            markdown.append('\n');
        }
        markdown.append('\n');
    }
    return file.write(markdown) == markdown.size() && file.commit();
}

bool AppController::copyAiText(const QString &text)
{
    return m_aiClipboard != nullptr && m_aiClipboard->setProtectedText(text);
}

bool AppController::attachAiSelection()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->aiError.clear();
    if (tab->ssh)
    {
        tab->ssh->requestSelectedText();
        emit aiConversationChanged();
        return true;
    }
    if (tab->local)
    {
        tab->local->requestSelectedText();
        emit aiConversationChanged();
        return true;
    }
    return false;
}

bool AppController::attachAiRecentCommands(const int count)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->semanticObserver || count < 1)
    {
        return false;
    }
    const auto snapshot = tab->semanticObserver->snapshot();
    const auto maximum = static_cast<std::size_t>(std::clamp(count, 1, 5));
    std::vector<const terminal::CommandBlock *> selected;
    selected.reserve(maximum);
    for (const auto &commandBlock : std::views::reverse(snapshot.commandBlocks))
    {
        if (commandBlock.state != terminal::CommandBlockState::finished)
        {
            continue;
        }
        selected.push_back(&commandBlock);
        if (selected.size() == maximum)
        {
            break;
        }
    }
    std::erase_if(tab->aiExplicitContextItems, [](const ai::AiExplicitContext &item) {
        return item.source == "terminal_command";
    });
    if (selected.empty())
    {
        std::vector<QString> commands;
        commands.reserve(maximum);
        const auto appendRecent = [&commands, maximum](const std::vector<workbench::ShellHistoryEntry> &entries) {
            for (const auto &entry : entries)
            {
                const QString command = utf8QString(entry.command).trimmed();
                if (command.isEmpty() || std::ranges::find(commands, command) != commands.end())
                {
                    continue;
                }
                commands.push_back(command);
                if (commands.size() == maximum)
                {
                    break;
                }
            }
        };
        appendRecent(tab->capturedHistory);
        if (commands.size() < maximum)
        {
            appendRecent(tab->history);
        }

        const std::size_t recentLineCount = maximum * std::size_t{40};
        std::expected<terminal::TerminalScrollbackPage, std::error_code> recentPage =
            std::unexpected(std::make_error_code(std::errc::not_connected));
        const terminal::TerminalScrollbackRequest scrollbackRequest{.anchor = terminal::TerminalScrollbackAnchor::tail,
                                                                    .offset = 0,
                                                                    .lineCount = recentLineCount};
        if (tab->ssh)
        {
            recentPage = tab->ssh->scrollbackPage(scrollbackRequest);
        }
        else if (tab->local)
        {
            recentPage = tab->local->scrollbackPage(scrollbackRequest);
        }
        QString recentOutput;
        if (recentPage.has_value())
        {
            const ai::AiTerminalOutputRequest outputRequest{
                .target = {.sessionId = utf8String(tab->id), .sessionGeneration = tab->reconnectGeneration},
                .anchor = terminal::TerminalScrollbackAnchor::tail,
                .offset = 0,
                .lineCount = recentLineCount,
                .maximumBytes = std::size_t{16} * 1024};
            recentOutput = utf8QString(ai::AiTerminalOutputTool::read(outputRequest, *recentPage).content).trimmed();
        }
        if (recentOutput.isEmpty())
        {
            recentOutput = terminalFrameText(tab->snapshot).trimmed();
        }
        if (commands.empty() && recentOutput.isEmpty())
        {
            tab->aiError = tr("No recent terminal activity is available to attach.");
            emit aiConversationChanged();
            return false;
        }

        QString content =
            tr("Approximate terminal context. Command and output boundaries are not available for this shell.");
        if (!commands.empty())
        {
            content += tr("\nRecent commands (newest first):");
            for (const QString &command : commands)
            {
                content += QStringLiteral("\n$ ") + command;
            }
        }
        if (!recentOutput.isEmpty())
        {
            content += tr("\nRecent terminal output:\n%1").arg(recentOutput);
        }
        tab->aiExplicitContextItems.push_back(
            ai::AiExplicitContext{.id = "terminal-command:approximate",
                                  .title = utf8String(tr("Recent terminal activity (approximate)")),
                                  .content = utf8String(content),
                                  .source = "terminal_command"});
        tab->aiError.clear();
        static_cast<void>(buildAiContext(*tab, false));
        emit aiConversationChanged();
        return true;
    }
    for (const auto *commandBlock : std::views::reverse(selected))
    {
        const auto &block = *commandBlock;
        const auto outputBytes = QByteArray(reinterpret_cast<const char *>(block.retainedOutput.data()),
                                            static_cast<qsizetype>(block.retainedOutput.size()));
        QString content = tr("Command: %1").arg(utf8QString(block.command));
        if (block.exitStatus.has_value())
        {
            content += tr("\nExit status: %1").arg(*block.exitStatus);
        }
        if (!outputBytes.isEmpty())
        {
            content += tr("\nOutput:\n%1").arg(QString::fromUtf8(outputBytes));
        }
        tab->aiExplicitContextItems.push_back(ai::AiExplicitContext{
            .id = "terminal-command:" + std::to_string(block.id),
            .title = utf8String(tr("Terminal command: %1").arg(utf8QString(block.command).left(80))),
            .content = utf8String(content),
            .source = "terminal_command"});
    }
    tab->aiError.clear();
    static_cast<void>(buildAiContext(*tab, false));
    emit aiConversationChanged();
    return true;
}

bool AppController::attachAiTextFiles(const QStringList &localFileUrls)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || localFileUrls.isEmpty())
    {
        return false;
    }
    if (localFileUrls.size() > static_cast<qsizetype>(maximumAiTextAttachmentFiles))
    {
        tab->aiError = tr("Attach no more than four text files to one message.");
        emit aiConversationChanged();
        return false;
    }

    QStringList paths;
    paths.reserve(localFileUrls.size());
    for (const QString &value : localFileUrls)
    {
        const QUrl url(value);
        const QString path = url.isLocalFile() ? url.toLocalFile() : value;
        if (path.trimmed().isEmpty())
        {
            return false;
        }
        paths.push_back(path);
    }

    tab->aiError.clear();
    const QString tabId = tab->id;
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, tabId, paths = std::move(paths)] {
        AiTextAttachmentLoadResult result;
        result.attachments.reserve(static_cast<std::size_t>(paths.size()));
        for (const QString &path : paths)
        {
            const QFileInfo info(path);
            const QString canonicalPath = info.canonicalFilePath();
            if (canonicalPath.isEmpty() || !info.isFile() || info.size() < 0
                || info.size() > maximumAiTextAttachmentBytes)
            {
                result.rejectedFiles.push_back(info.fileName().isEmpty() ? path : info.fileName());
                continue;
            }

            QFile file(canonicalPath);
            if (!file.open(QIODevice::ReadOnly))
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }
            const QByteArray bytes = file.readAll();
            if (bytes.size() != info.size() || bytes.contains('\0'))
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString text = decoder(bytes);
            if (decoder.hasError())
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }

            const QByteArray digest =
                QCryptographicHash::hash(canonicalPath.toUtf8(), QCryptographicHash::Sha256).toHex();
            const QString title = info.fileName();
            const QString content = title + QStringLiteral("\n\n") + text;
            result.attachments.push_back(ai::AiExplicitContext{.id = "local-file:" + digest.toStdString(),
                                                               .title = utf8String(title),
                                                               .content = utf8String(content),
                                                               .source = "local_file"});
        }

        if (!self)
        {
            return;
        }
        emit self->aiTextAttachmentTaskCompleted(tabId, std::move(result.attachments), result.rejectedFiles);
    });
    emit aiConversationChanged();
    return true;
}

void AppController::applyAiTextAttachmentTaskResult(const QString &tabId, AiTextAttachments attachments,
                                                    const QStringList &rejectedFiles)
{
    TerminalTab *target = findTab(tabId);
    if (target == nullptr)
    {
        return;
    }
    for (auto &attachment : attachments)
    {
        const auto existing =
            std::ranges::find(target->aiExplicitContextItems, attachment.id, &ai::AiExplicitContext::id);
        target->aiExcludedContextIds.erase("attachment:" + attachment.id);
        if (existing == target->aiExplicitContextItems.end())
        {
            target->aiExplicitContextItems.push_back(std::move(attachment));
        }
        else
        {
            *existing = std::move(attachment);
        }
    }
    target->aiError = rejectedFiles.isEmpty()
                          ? QString{}
                          : tr("Could not attach these text files: %1").arg(rejectedFiles.join(QStringLiteral(", ")));
    static_cast<void>(buildAiContext(*target, false));
    emit aiConversationChanged();
}

bool AppController::attachAiImageFiles(const QStringList &localFileUrls)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || localFileUrls.isEmpty()
        || localFileUrls.size() > static_cast<qsizetype>(maximumAiImageAttachmentFiles)
        || tab->aiImageAttachments.size() + static_cast<std::size_t>(localFileUrls.size())
               > static_cast<std::size_t>(maximumAiImageAttachmentFiles))
    {
        if (tab != nullptr)
        {
            tab->aiError = tr("Attach no more than four images to one message.");
            emit aiConversationChanged();
        }
        return false;
    }

    QStringList paths;
    paths.reserve(localFileUrls.size());
    for (const QString &value : localFileUrls)
    {
        const QUrl url(value);
        const QString path = url.isLocalFile() ? url.toLocalFile() : value;
        if (path.trimmed().isEmpty())
        {
            return false;
        }
        paths.push_back(path);
    }

    const auto existingBytes =
        std::accumulate(tab->aiImageAttachments.begin(), tab->aiImageAttachments.end(), qsizetype{0},
                        [](const qsizetype total, const ai::AiImageAttachment &image) {
                            return total + static_cast<qsizetype>(image.byteSize);
                        });
    tab->aiError.clear();
    const QString tabId = tab->id;
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, tabId, paths = std::move(paths), existingBytes] {
        AiImageAttachmentLoadResult result;
        result.attachments.reserve(static_cast<std::size_t>(paths.size()));
        qsizetype totalBytes = existingBytes;
        for (const QString &path : paths)
        {
            const QFileInfo info(path);
            const QString canonicalPath = info.canonicalFilePath();
            if (canonicalPath.isEmpty() || !info.isFile() || info.size() <= 0
                || info.size() > maximumAiImageAttachmentBytes
                || totalBytes + info.size() > maximumAiImageAttachmentTotalBytes)
            {
                result.rejectedFiles.push_back(info.fileName().isEmpty() ? path : info.fileName());
                continue;
            }

            QFile file(canonicalPath);
            if (!file.open(QIODevice::ReadOnly))
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }
            const QByteArray bytes = file.readAll();
            if (bytes.size() != info.size())
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }

            auto attachment = aiImageAttachmentFromBytes(info.fileName(), bytes);
            if (!attachment.has_value())
            {
                result.rejectedFiles.push_back(info.fileName());
                continue;
            }
            totalBytes += static_cast<qsizetype>(attachment->byteSize);
            result.attachments.push_back(std::move(*attachment));
        }

        if (!self)
        {
            return;
        }
        emit self->aiImageAttachmentTaskCompleted(tabId, std::move(result.attachments), result.rejectedFiles);
    });
    emit aiConversationChanged();
    return true;
}

bool AppController::attachAiClipboardContent()
{
    TerminalTab *tab = activeTab();
    QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard == nullptr ? nullptr : clipboard->mimeData();
    if (tab == nullptr || mimeData == nullptr)
    {
        return false;
    }

    if (mimeData->hasUrls())
    {
        QStringList imageUrls;
        QStringList textUrls;
        for (const QUrl &url : mimeData->urls())
        {
            if (!url.isLocalFile())
            {
                continue;
            }
            const QString value = url.toString();
            (isAiImageFile(url.toLocalFile()) ? imageUrls : textUrls).push_back(value);
        }
        const bool hasLocalFiles = !imageUrls.isEmpty() || !textUrls.isEmpty();
        if (!imageUrls.isEmpty())
        {
            static_cast<void>(attachAiImageFiles(imageUrls));
        }
        if (!textUrls.isEmpty())
        {
            static_cast<void>(attachAiTextFiles(textUrls));
        }
        if (hasLocalFiles)
        {
            return true;
        }
    }

    if (!mimeData->hasImage())
    {
        return false;
    }
    if (tab->aiImageAttachments.size() >= static_cast<std::size_t>(maximumAiImageAttachmentFiles))
    {
        tab->aiError = tr("Attach no more than four images to one message.");
        emit aiConversationChanged();
        return true;
    }

    QImage image = clipboard->image();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0
        || static_cast<quint64>(image.width()) * static_cast<quint64>(image.height()) > maximumAiImagePixels)
    {
        tab->aiError = tr("Could not attach the clipboard image.");
        emit aiConversationChanged();
        return true;
    }

    const auto existingBytes =
        std::accumulate(tab->aiImageAttachments.begin(), tab->aiImageAttachments.end(), qsizetype{0},
                        [](const qsizetype total, const ai::AiImageAttachment &attachment) {
                            return total + static_cast<qsizetype>(attachment.byteSize);
                        });
    tab->aiError.clear();
    const QString tabId = tab->id;
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, tabId, image = std::move(image), existingBytes] {
        AiImageAttachmentLoadResult result;
        constexpr auto clipboardFileName = "clipboard-image.png";
        QByteArray bytes;
        QBuffer output(&bytes);
        std::optional<ai::AiImageAttachment> attachment;
        if (output.open(QIODevice::WriteOnly) && image.save(&output, "PNG") && !bytes.isEmpty()
            && existingBytes + bytes.size() <= maximumAiImageAttachmentTotalBytes)
        {
            attachment = aiImageAttachmentFromBytes(QString::fromLatin1(clipboardFileName), std::move(bytes));
        }
        if (attachment.has_value())
        {
            result.attachments.push_back(std::move(*attachment));
        }
        else
        {
            result.rejectedFiles.push_back(QString::fromLatin1(clipboardFileName));
        }
        if (self)
        {
            emit self->aiImageAttachmentTaskCompleted(tabId, std::move(result.attachments), result.rejectedFiles);
        }
    });
    emit aiConversationChanged();
    return true;
}

void AppController::applyAiImageAttachmentTaskResult(const QString &tabId, AiImageAttachments attachments,
                                                     const QStringList &rejectedFiles)
{
    TerminalTab *target = findTab(tabId);
    if (target == nullptr)
    {
        return;
    }
    for (auto &attachment : attachments)
    {
        const auto existing = std::ranges::find(target->aiImageAttachments, attachment.id, &ai::AiImageAttachment::id);
        if (existing == target->aiImageAttachments.end())
        {
            target->aiImageAttachments.push_back(std::move(attachment));
        }
        else
        {
            *existing = std::move(attachment);
        }
    }
    target->aiError = rejectedFiles.isEmpty()
                          ? QString{}
                          : tr("Could not attach these images: %1").arg(rejectedFiles.join(QStringLiteral(", ")));
    static_cast<void>(buildAiContext(*target, false));
    emit aiConversationChanged();
}

bool AppController::removeAiContextItem(const QString &itemId)
{
    TerminalTab *tab = activeTab();
    const std::string id = utf8String(itemId.trimmed());
    if (tab == nullptr || id.empty())
    {
        return false;
    }
    const auto image = std::ranges::find(tab->aiImageAttachments, id, &ai::AiImageAttachment::id);
    if (image != tab->aiImageAttachments.end())
    {
        tab->aiImageAttachments.erase(image);
        static_cast<void>(buildAiContext(*tab, false));
        emit aiConversationChanged();
        return true;
    }
    tab->aiPinnedContextIds.erase(id);
    const bool changed = tab->aiExcludedContextIds.insert(id).second;
    static_cast<void>(buildAiContext(*tab, false));
    emit aiConversationChanged();
    return changed;
}

bool AppController::setAiContextItemPinned(const QString &itemId, const bool pinned)
{
    TerminalTab *tab = activeTab();
    const std::string id = utf8String(itemId.trimmed());
    if (tab == nullptr || id.empty())
    {
        return false;
    }
    tab->aiExcludedContextIds.erase(id);
    const bool changed = pinned ? tab->aiPinnedContextIds.insert(id).second : tab->aiPinnedContextIds.erase(id) != 0;
    static_cast<void>(buildAiContext(*tab, false));
    emit aiConversationChanged();
    return changed;
}

void AppController::resetAiContextItems()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    // Restore the automatic context set: drop removals and pins AND clear any
    // explicit attachments, so the panel visibly returns to the default
    // automatic items (or to an empty set when automatic context is off).
    tab->aiExcludedContextIds.clear();
    tab->aiPinnedContextIds.clear();
    tab->aiExplicitContextItems.clear();
    tab->aiImageAttachments.clear();
    static_cast<void>(buildAiContext(*tab, false));
    emit aiConversationChanged();
}

ai::AiContextBundle AppController::buildAiContext(TerminalTab &tab, const bool preferLastFailure)
{
    terminal::SemanticTerminalSnapshot semanticSnapshot;
    if (tab.semanticObserver)
    {
        semanticSnapshot = tab.semanticObserver->snapshot();
    }
    terminal::TerminalSemanticCapability capability = semanticCapability(semanticSnapshot);
    QString shell = tab.kind == TerminalTabKind::Local ? QStringLiteral("pwsh") : QString{};
    if (!semanticSnapshot.commandBlocks.empty())
    {
        if (!semanticSnapshot.commandBlocks.back().shell.empty())
        {
            shell = utf8QString(semanticSnapshot.commandBlocks.back().shell);
        }
    }

    std::vector<ai::AiExplicitContext> explicitItems = tab.aiExplicitContextItems;
    for (auto &item : explicitItems)
    {
        if (item.source == "terminal_selection")
        {
            item.title = utf8String(tr("Terminal selection"));
        }
    }

    ai::AiContextRequest contextRequest{
        .preferLastFailure = preferLastFailure,
        .automaticContextEnabled = m_settings.aiAutomaticContext,
        .explicitItems = std::move(explicitItems),
        .currentFrame = ai::AiTerminalFrameContext{.id = utf8String(tab.id),
                                                   .content = utf8String(terminalFrameText(tab.snapshot)),
                                                   .sessionId = utf8String(tab.id),
                                                   .host = utf8String(tab.address),
                                                   .shell = utf8String(shell),
                                                   .workingDirectory = utf8String(tab.terminalWorkingDirectory),
                                                   .sessionGeneration = tab.reconnectGeneration,
                                                   .capability = capability},
        .excludedItemIds = tab.aiExcludedContextIds,
        .pinnedItemIds = tab.aiPinnedContextIds};
    ai::AiContextBundle context = m_aiContextBroker.build(semanticSnapshot.commandBlocks, contextRequest);
    const auto serialized = ai::AiContextSerializer::serialize(context);
    tab.aiContextPreview = utf8QString(serialized.text);
    tab.aiContextItems.clear();
    tab.aiContextItems.reserve(static_cast<qsizetype>(context.items.size()));
    for (const auto &item : context.items)
    {
        QString kind = QStringLiteral("command");
        if (item.kind == ai::AiContextItemKind::explicitAttachment)
        {
            kind = QStringLiteral("attachment");
        }
        else if (item.kind == ai::AiContextItemKind::currentTerminalFrame)
        {
            kind = QStringLiteral("frame");
        }
        QString preview;
        if (!item.command.empty())
        {
            preview = QStringLiteral("$ ") + utf8QString(item.command);
        }
        if (!item.content.empty())
        {
            if (!preview.isEmpty())
            {
                preview += QLatin1Char('\n');
            }
            preview += utf8QString(item.content);
        }
        tab.aiContextItems.push_back(QVariantMap{{QStringLiteral("id"), utf8QString(item.id)},
                                                 {QStringLiteral("title"), utf8QString(item.title)},
                                                 {QStringLiteral("kind"), kind},
                                                 {QStringLiteral("preview"), preview},
                                                 {QStringLiteral("quality"), semanticCapabilityToken(item.capability)},
                                                 {QStringLiteral("redacted"), item.redacted},
                                                 {QStringLiteral("truncated"), item.truncated},
                                                 {QStringLiteral("pinned"), item.pinned},
                                                 {QStringLiteral("automatic"), item.automatic}});
    }
    for (const auto &image : tab.aiImageAttachments)
    {
        const QString previewBase64 =
            QString::fromLatin1(image.previewBase64Data.data(), static_cast<qsizetype>(image.previewBase64Data.size()));
        tab.aiContextItems.push_back(
            QVariantMap{{QStringLiteral("id"), utf8QString(image.id)},
                        {QStringLiteral("title"), utf8QString(image.fileName)},
                        {QStringLiteral("kind"), QStringLiteral("image")},
                        {QStringLiteral("quality"), QStringLiteral("image")},
                        {QStringLiteral("redacted"), false},
                        {QStringLiteral("truncated"), false},
                        {QStringLiteral("pinned"), false},
                        {QStringLiteral("automatic"), false},
                        {QStringLiteral("mediaType"), utf8QString(image.mediaType)},
                        {QStringLiteral("byteSize"), QVariant::fromValue<qulonglong>(image.byteSize)},
                        {QStringLiteral("pixelWidth"), image.pixelWidth},
                        {QStringLiteral("pixelHeight"), image.pixelHeight},
                        {QStringLiteral("previewUrl"), QStringLiteral("data:image/png;base64,%1").arg(previewBase64)}});
    }
    return context;
}

ai::AiTerminalReadSnapshot AppController::aiReadSnapshot(const TerminalTab &tab) const
{
    terminal::SemanticTerminalSnapshot semantic;
    if (tab.semanticObserver)
    {
        semantic = tab.semanticObserver->snapshot();
    }
    auto capability = semanticCapability(semantic);
    QString shell = tab.kind == TerminalTabKind::Local ? QStringLiteral("pwsh") : QString{};
    if (!semantic.commandBlocks.empty())
    {
        if (!semantic.commandBlocks.back().shell.empty())
        {
            shell = utf8QString(semantic.commandBlocks.back().shell);
        }
    }
    ai::AiOperationsReadSnapshot operations;
    operations.sftpState = utf8String(tab.sftpState);
    operations.sftpPath = utf8String(tab.sftpPath);
    operations.sftpHomePath = utf8String(tab.sftpHomePath);
    operations.sftpListingAvailable = tab.sftpHasListing && tab.sftpModel != nullptr;
    if (tab.sftpModel)
    {
        constexpr int maximumSftpSnapshotEntries = 500;
        const int rowCount = std::min(tab.sftpModel->rowCount(), maximumSftpSnapshotEntries);
        operations.sftpEntries.reserve(static_cast<std::size_t>(rowCount));
        for (int row = 0; row < rowCount; ++row)
        {
            const QVariantMap value = tab.sftpModel->entry(row);
            const QVariant modified = value.value(QStringLiteral("modifiedUtcSeconds"));
            operations.sftpEntries.push_back(ai::AiSftpEntrySnapshot{
                .name = utf8String(value.value(QStringLiteral("name")).toString()),
                .remotePath = utf8String(value.value(QStringLiteral("remotePath")).toString()),
                .type = utf8String(value.value(QStringLiteral("entryType")).toString()),
                .size = value.value(QStringLiteral("size")).toULongLong(),
                .modifiedUtcSeconds =
                    modified.isValid() ? std::optional<std::int64_t>{modified.toLongLong()} : std::nullopt,
                .permissions = utf8String(value.value(QStringLiteral("permissions")).toString()),
                .hidden = value.value(QStringLiteral("hidden")).toBool()});
        }
    }
    const auto &history = tab.history.empty() ? tab.capturedHistory : tab.history;
    constexpr std::size_t maximumHistorySnapshotEntries = 500;
    const std::size_t historyStart =
        history.size() > maximumHistorySnapshotEntries ? history.size() - maximumHistorySnapshotEntries : 0;
    operations.shellHistory.reserve(history.size() - historyStart);
    for (std::size_t index = historyStart; index < history.size(); ++index)
    {
        const auto &entry = history[index];
        operations.shellHistory.push_back(
            ai::AiShellHistorySnapshot{.command = utf8String(utf8QString(entry.command).left(1024)),
                                       .shell = utf8String(shellKindToken(entry.shell)),
                                       .timestampUtcSeconds = entry.timestampUtcSeconds});
    }
    operations.scripts.reserve(m_scripts.size());
    for (const auto &script : m_scripts)
    {
        ai::AiScriptSnapshot snapshot{.id = script.id,
                                      .name = script.name,
                                      .description = utf8String(utf8QString(script.description).left(4096)),
                                      .shell = utf8String(quickCommandShellScopeToken(script.shellScope)),
                                      .variableCount = script.variables.size(),
                                      .stepCount = script.steps.size(),
                                      .modifiedUtcMs = script.modifiedUtcMs};
        snapshot.variables.reserve(script.variables.size());
        for (const auto &variable : script.variables)
        {
            snapshot.variables.push_back(
                ai::AiScriptSnapshot::Variable{.name = variable.name,
                                               .label = variable.label,
                                               .type = utf8String(scriptVariableTypeToken(variable.type)),
                                               .choices = variable.choices,
                                               .required = variable.required});
        }
        snapshot.steps.reserve(script.steps.size());
        for (const auto &step : script.steps)
        {
            snapshot.steps.push_back(
                ai::AiScriptSnapshot::Step{.command = step.command,
                                           .continuation = utf8String(scriptContinuationToken(step.continuation)),
                                           .outputMarker = step.outputMarker,
                                           .timeoutMs = step.timeoutMs});
        }
        operations.scripts.push_back(std::move(snapshot));
    }
    operations.notes.reserve(static_cast<std::size_t>(m_notes.size()));
    for (const QVariant &noteValue : m_notes)
    {
        const QVariantMap note = noteValue.toMap();
        operations.notes.push_back(
            ai::AiNoteSnapshot{.path = utf8String(note.value(QStringLiteral("path")).toString()),
                               .name = utf8String(note.value(QStringLiteral("name")).toString()),
                               .size = note.value(QStringLiteral("size")).toULongLong(),
                               .modifiedUtcMs = note.value(QStringLiteral("modifiedUtcMs")).toLongLong(),
                               .folder = note.value(QStringLiteral("folder")).toBool()});
    }
    operations.portForwarding.reserve(m_portForwardingRules.size());
    for (const auto &rule : m_portForwardingRules)
    {
        if (tab.sourceProfileId.isEmpty() || rule.profileId != utf8String(tab.sourceProfileId))
        {
            continue;
        }
        const auto profile = std::ranges::find(m_profiles, rule.profileId, &ssh::SshProfile::id);
        const PortForwardingRuntime *runtime = findPortForwardingRuntime(rule.id);
        const forwarding::PortForwardingJobSnapshot forwardingSnapshot =
            runtime != nullptr && runtime->job ? runtime->job->snapshot() : forwarding::PortForwardingJobSnapshot{};
        operations.portForwarding.push_back(
            ai::AiPortForwardingSnapshot{.id = rule.id,
                                         .label = rule.label,
                                         .profileName = profile == m_profiles.end() ? std::string{} : profile->name,
                                         .type = utf8String(forwardingTypeToken(rule.type)),
                                         .bindHost = rule.bind.host,
                                         .bindPort = rule.bind.port,
                                         .destinationHost = rule.destination.host,
                                         .destinationPort = rule.destination.port,
                                         .state = utf8String(forwardingStateToken(forwardingSnapshot.state)),
                                         .failure = utf8String(forwardingFailureToken(forwardingSnapshot.failure)),
                                         .activeClients = forwardingSnapshot.activeClients,
                                         .bytesFromClients = forwardingSnapshot.bytesFromClients,
                                         .bytesToClients = forwardingSnapshot.bytesToClients,
                                         .rejectedClients = forwardingSnapshot.rejectedClients});
    }
    operations.telemetry.state = utf8String(tab.telemetryState);
    if (tab.telemetrySample.has_value())
    {
        const auto &sample = *tab.telemetrySample;
        operations.telemetry.osName = sample.osName;
        operations.telemetry.cpuPercent = sample.cpuPercent;
        operations.telemetry.cpuCoreCount = sample.cpuCoreCount;
        operations.telemetry.memoryUsedKiB = sample.memoryUsedKiB;
        operations.telemetry.memoryTotalKiB = sample.memory.totalKiB;
        operations.telemetry.receivedBytesPerSecond = sample.receivedBytesPerSecond;
        operations.telemetry.transmittedBytesPerSecond = sample.transmittedBytesPerSecond;
        operations.telemetry.sshProbeLatencyMs = sample.sshProbeLatencyMs;
    }
    ai::AiCommandOutputReader commandOutputReader;
    if (tab.semanticObserver)
    {
        commandOutputReader = [observer = tab.semanticObserver](const terminal::CommandBlockId id,
                                                                const std::uint64_t afterCursor,
                                                                const std::size_t maximumBytes) {
            return observer->readCommandOutput(id, afterCursor, maximumBytes);
        };
    }
    return ai::AiTerminalReadSnapshot{.sessionId = utf8String(tab.id),
                                      .title = utf8String(tab.title),
                                      .host = utf8String(tab.address),
                                      .shell = utf8String(shell),
                                      .workingDirectory = utf8String(tab.terminalWorkingDirectory),
                                      .terminalFrame = utf8String(terminalFrameText(tab.snapshot)),
                                      .sessionGeneration = tab.reconnectGeneration,
                                      .capability = capability,
                                      .connected = tab.running,
                                      .commandBlocks = std::move(semantic.commandBlocks),
                                      .operations = std::move(operations),
                                      .commandOutputReader = std::move(commandOutputReader)};
}

void AppController::acceptAiSelectedText(TerminalTab &tab, const QString &text)
{
    if (text.trimmed().isEmpty())
    {
        tab.aiError = tr("Select terminal text before attaching it.");
        emit aiConversationChanged();
        return;
    }

    const std::string itemId = "terminal-selection:" + utf8String(tab.id);
    const auto maximumCharacters = static_cast<qsizetype>(m_aiContextBroker.limits().maxItemBytes);
    ai::AiExplicitContext selection{.id = itemId,
                                    .title = utf8String(tr("Terminal selection")),
                                    .content = utf8String(text.left(maximumCharacters)),
                                    .source = "terminal_selection"};
    const auto existing = std::ranges::find(tab.aiExplicitContextItems, itemId, &ai::AiExplicitContext::id);
    if (existing == tab.aiExplicitContextItems.end())
    {
        tab.aiExplicitContextItems.push_back(std::move(selection));
    }
    else
    {
        *existing = std::move(selection);
    }
    tab.aiExcludedContextIds.erase(itemId);
    tab.aiError.clear();
    static_cast<void>(buildAiContext(tab, false));
    emit aiConversationChanged();
}

void AppController::recordAiActivity(const TerminalTab &tab, const ai::AiToolCall &call, const QString &state,
                                     const QString &resultCode, const bool sideEffecting, const bool highRisk)
{
    QString summary;
    if (call.name == "run_command")
    {
        QJsonParseError parseError;
        const QJsonDocument arguments =
            QJsonDocument::fromJson(QByteArray::fromStdString(call.argumentsJson), &parseError);
        if (parseError.error == QJsonParseError::NoError && arguments.isObject())
        {
            summary = arguments.object().value(QStringLiteral("command")).toString().left(1024);
        }
    }
    if (tab.aiConversation && tab.aiAssistantMessageId != 0)
    {
        const bool activityUpdated = tab.aiConversation->upsertAssistantToolActivity(
            tab.aiAssistantMessageId, utf8QString(call.id), utf8QString(call.name), summary, state, resultCode,
            sideEffecting, highRisk);
        if (activityUpdated)
        {
            static_cast<void>(tab.aiConversation->setAssistantToolDetails(
                tab.aiAssistantMessageId, utf8QString(call.id), aiToolDetailText(call.argumentsJson), {}));
        }
    }
    m_aiActivity.record({.conversationId = tab.aiConversationId,
                         .toolCallId = utf8QString(call.id),
                         .toolName = utf8QString(call.name),
                         .state = state,
                         .resultCode = resultCode,
                         .permissionMode = config::aiPermissionPreferenceToken(m_settings.aiPermission),
                         .sessionGeneration = tab.reconnectGeneration,
                         .sideEffecting = sideEffecting,
                         .highRisk = highRisk});
}

void AppController::handleAiStreamEvent(const QString &tabId, const std::uint64_t assistantMessageId,
                                        const ai::AiStreamEvent &event)
{
    TerminalTab *target = findTab(tabId);
    if (target == nullptr || !target->aiConversation || target->aiAssistantMessageId != assistantMessageId)
    {
        return;
    }
    switch (event.type)
    {
        case ai::AiStreamEventType::responseStarted:
            target->aiState = QStringLiteral("streaming");
            break;
        case ai::AiStreamEventType::textDelta:
            static_cast<void>(
                target->aiConversation->appendAssistantDelta(assistantMessageId, utf8QString(event.delta)));
            break;
        case ai::AiStreamEventType::usageUpdated:
            if (event.usage.has_value())
            {
                if (!target->aiUsage.has_value())
                {
                    target->aiUsage = event.usage;
                }
                else
                {
                    target->aiUsage->inputTokens += event.usage->inputTokens;
                    target->aiUsage->outputTokens += event.usage->outputTokens;
                    target->aiUsage->reasoningTokens += event.usage->reasoningTokens;
                    target->aiUsage->cachedInputTokens += event.usage->cachedInputTokens;
                }
                static_cast<void>(target->aiConversation->updateAssistantUsage(assistantMessageId, *target->aiUsage));
            }
            break;
        case ai::AiStreamEventType::responseCompleted:
            static_cast<void>(target->aiConversation->setAssistantProviderReplay(
                assistantMessageId, event.providerToolHistory, event.providerAssistantContentJson));
            if (target->aiConversation->completeAssistantMessage(assistantMessageId, target->aiUsage))
            {
                persistAiConversation(*target);
            }
            target->aiState = QStringLiteral("complete");
            target->aiError.clear();
            target->aiProviderErrorCode.reset();
            target->aiProviderRetryAfterMilliseconds.reset();
            break;
        case ai::AiStreamEventType::responseFailed:
            if (event.error.has_value() && event.error->code == ai::AiProviderErrorCode::cancelled)
            {
                target->aiError.clear();
                target->aiProviderErrorCode = ai::AiProviderErrorCode::cancelled;
                target->aiProviderRetryAfterMilliseconds.reset();
                static_cast<void>(target->aiConversation->cancelAssistantMessage(assistantMessageId));
                target->aiState = QStringLiteral("cancelled");
            }
            else
            {
                const auto providerError = event.error.value_or(ai::AiProviderError{});
                target->aiError = utf8QString(providerError.message);
                target->aiProviderErrorCode = providerError.code;
                target->aiProviderRetryAfterMilliseconds = providerError.retryAfterMilliseconds;
                QStringList details;
                if (providerError.httpStatus.has_value())
                {
                    details.append(tr("HTTP %1").arg(*providerError.httpStatus));
                }
                if (!providerError.providerCode.empty())
                {
                    details.append(tr("Code: %1").arg(utf8QString(providerError.providerCode)));
                }
                if (!providerError.requestId.empty())
                {
                    details.append(tr("Request: %1").arg(utf8QString(providerError.requestId)));
                }
                if (!details.isEmpty())
                {
                    target->aiError += QLatin1Char('\n') + details.join(QStringLiteral(" · "));
                }
                static_cast<void>(target->aiConversation->failAssistantMessage(assistantMessageId, target->aiError));
                persistAiConversation(*target);
                target->aiState = QStringLiteral("error");
            }
            break;
        case ai::AiStreamEventType::reasoningDelta:
            static_cast<void>(
                target->aiConversation->appendAssistantReasoningDelta(assistantMessageId, utf8QString(event.delta)));
            break;
        case ai::AiStreamEventType::webSearchStarted:
        {
            const QString callId =
                !event.toolCallId.empty() ? utf8QString(event.toolCallId) : utf8QString(event.itemId);
            if (!callId.isEmpty())
            {
                static_cast<void>(target->aiConversation->upsertAssistantToolActivity(
                    assistantMessageId, callId, QStringLiteral("web_search"), tr("Searching the web"),
                    QStringLiteral("running"), {}, false, false));
            }
            break;
        }
        case ai::AiStreamEventType::webSearchQuery:
        {
            const QString callId =
                !event.toolCallId.empty() ? utf8QString(event.toolCallId) : utf8QString(event.itemId);
            const QString query = utf8QString(event.delta);
            if (!callId.isEmpty())
            {
                target->aiWebSearchQueries.insert(callId, query);
                static_cast<void>(target->aiConversation->upsertAssistantToolActivity(
                    assistantMessageId, callId, QStringLiteral("web_search"), query, QStringLiteral("running"), {},
                    false, false));
            }
            break;
        }
        case ai::AiStreamEventType::webSearchCompleted:
        {
            const QString callId =
                !event.toolCallId.empty() ? utf8QString(event.toolCallId) : utf8QString(event.itemId);
            if (!callId.isEmpty())
            {
                const QString resultCode = utf8QString(event.delta);
                const QString summary = target->aiWebSearchQueries.value(callId, tr("Web search"));
                static_cast<void>(target->aiConversation->upsertAssistantToolActivity(
                    assistantMessageId, callId, QStringLiteral("web_search"), summary,
                    resultCode.isEmpty() ? QStringLiteral("succeeded") : QStringLiteral("failed"), resultCode, false,
                    false));
            }
            break;
        }
        case ai::AiStreamEventType::webSourceAdded:
            if (event.webSource.has_value())
            {
                static_cast<void>(target->aiConversation->appendAssistantSource(assistantMessageId, *event.webSource));
            }
            break;
        case ai::AiStreamEventType::toolCallStarted:
        case ai::AiStreamEventType::toolArgumentsDelta:
        case ai::AiStreamEventType::toolCallCompleted:
        case ai::AiStreamEventType::reasoningSignatureDelta:
            break;
    }
    if (m_activeTabId == tabId || m_focusedTabId == tabId)
    {
        emit aiConversationChanged();
    }
}

bool AppController::aiTurnActive(const TerminalTab &tab) const noexcept
{
    return tab.aiTurnRunner && tab.aiTurnRunner->active();
}

ai::AiTurnRunner::TurnId AppController::activeAiTurnId(const TerminalTab &tab) const noexcept
{
    return tab.aiTurnRunner ? tab.aiTurnRunner->activeTurnId() : 0;
}

std::optional<ai::AiToolCall> AppController::pendingAiToolCall(const TerminalTab &tab) const
{
    return tab.aiTurnRunner ? tab.aiTurnRunner->pendingToolCall() : std::nullopt;
}

bool AppController::completePendingAiTool(TerminalTab &tab, const ai::AiToolOutput &output)
{
    const auto pending = pendingAiToolCall(tab);
    if (pending.has_value() && pending->id == output.callId && tab.aiConversation && tab.aiAssistantMessageId != 0)
    {
        static_cast<void>(tab.aiConversation->setAssistantToolDetails(
            tab.aiAssistantMessageId, utf8QString(output.callId), aiToolDetailText(pending->argumentsJson),
            aiToolDetailText(output.outputJson)));
    }
    return tab.aiTurnRunner && tab.aiTurnRunner->completePendingTool(output);
}

bool AppController::cancelAiTurn(TerminalTab &tab)
{
    return tab.aiTurnRunner && tab.aiTurnRunner->cancel();
}

std::expected<ai::AiTurnRunner::TurnId, ai::AiProviderError> AppController::startAiTurn(
    TerminalTab &tab, ai::AiProviderConfiguration configuration, ai::AiGenerationRequest generation,
    ai::AiTurnRunner::SecretLoader secretLoader, ai::AiTurnRunner::EventHandler eventHandler,
    ai::AiTurnRunner::FinishedHandler finishedHandler, ai::AiTurnRunner::RetryHandler retryHandler,
    ai::AiTurnRunner::JitterSource jitterSource, ai::AiTurnRunner::ToolHandler toolHandler,
    ai::AiTurnRunner::ToolOutputHandler toolOutputHandler, ai::AiTurnRunner::CompactionHandler compactionHandler)
{
    if (!tab.aiTurnRunner)
    {
        return std::unexpected(ai::AiProviderError{.code = ai::AiProviderErrorCode::invalidRequest,
                                                   .message = "The terminal AI runtime is unavailable.",
                                                   .retryable = false});
    }
    return tab.aiTurnRunner->start(std::move(configuration), std::move(generation), std::move(secretLoader),
                                   std::move(eventHandler), std::move(finishedHandler), std::move(retryHandler),
                                   std::move(jitterSource), std::move(toolHandler), std::move(toolOutputHandler),
                                   std::move(compactionHandler));
}

bool AppController::sendAiMessage(TerminalTab &tab, const QString &prompt, const bool preferLastFailure,
                                  const bool appendPrompt, const bool commandRequest,
                                  const QStringList &selectedSkillIds, const bool webSearchEnabled)
{
    const QString normalizedPrompt = prompt.trimmed();
    if ((normalizedPrompt.isEmpty() && tab.aiImageAttachments.empty()) || !tab.aiConversation || aiTurnActive(tab))
    {
        return false;
    }
    const bool chatGptSubscription = m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt;
    if (m_settings.aiModel.trimmed().isEmpty() || (!chatGptSubscription && m_settings.aiBaseUrl.trimmed().isEmpty()))
    {
        tab.aiError = tr("Configure an AI provider URL and model before starting a conversation.");
        emit aiConversationChanged();
        return false;
    }
    if (chatGptSubscription)
    {
        const ai::AiSecretStore store(m_credentialVaults->active());
        auto tokens =
            store.readOAuthTokens(aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString());
        if (!tokens.has_value())
        {
            tab.aiError = tr("Sign in with ChatGPT before starting a conversation.");
            emit aiConversationChanged();
            return false;
        }
        const auto expiration = ai::OpenAiSubscriptionAuthProtocol::expirationUtcSeconds(tokens->accessToken.view());
        constexpr qint64 refreshSkewSeconds = 120;
        if (expiration.has_value()
            && *expiration <= QDateTime::currentDateTimeUtc().toSecsSinceEpoch() + refreshSkewSeconds)
        {
            if (tab.aiState == QStringLiteral("authorizing"))
            {
                return false;
            }
            const QString tabId = tab.id;
            tab.aiState = QStringLiteral("authorizing");
            tab.aiError.clear();
            emit aiConversationChanged();
            withAiChatGptAccessToken([this, tabId, prompt, preferLastFailure, appendPrompt, commandRequest,
                                      selectedSkillIds, webSearchEnabled](auto accessToken, const QString &) {
                TerminalTab *target = findTab(tabId);
                if (target == nullptr || target->aiState != QStringLiteral("authorizing"))
                {
                    return;
                }
                if (!accessToken.has_value())
                {
                    target->aiState = QStringLiteral("error");
                    target->aiError = QString::fromStdString(accessToken.error().message);
                    emit aiConversationChanged();
                    return;
                }
                target->aiState = QStringLiteral("idle");
                static_cast<void>(sendAiMessage(*target, prompt, preferLastFailure, appendPrompt, commandRequest,
                                                selectedSkillIds, webSearchEnabled));
            });
            return true;
        }
    }
    if (webSearchEnabled && !aiWebSearchAvailable())
    {
        tab.aiError = tr("The selected provider protocol does not support native web search.");
        emit aiConversationChanged();
        return false;
    }

    QStringList resolvedSkillIds;
    resolvedSkillIds.reserve(selectedSkillIds.size());
    QSet<QString> seenSkillIds;
    for (const QString &rawId : selectedSkillIds)
    {
        const QString id = rawId.trimmed();
        if (id.isEmpty() || seenSkillIds.contains(id))
        {
            continue;
        }
        if (resolvedSkillIds.size() >= 4)
        {
            tab.aiError = tr("Select no more than four skills for one request.");
            emit aiConversationChanged();
            return false;
        }
        const QByteArray utf8Id = id.toUtf8();
        const std::string_view requestedId(utf8Id.constData(), static_cast<std::size_t>(utf8Id.size()));
        const auto found = std::ranges::find_if(m_aiUserSkills, [requestedId](const ai::AiUserSkill &skill) {
            return skill.ready && skill.id == requestedId;
        });
        if (found == m_aiUserSkills.end())
        {
            tab.aiError = tr("One selected skill is no longer available. Reload User skills and try again.");
            emit aiConversationChanged();
            return false;
        }
        seenSkillIds.insert(id);
        resolvedSkillIds.push_back(id);
    }

    const auto context = buildAiContext(tab, preferLastFailure);

    std::uint64_t userMessageId = 0;
    if (appendPrompt)
    {
        userMessageId = tab.aiConversation->appendUserMessage(normalizedPrompt, std::move(tab.aiImageAttachments),
                                                              aiContextAttachmentSummaries(context));
        tab.aiImageAttachments.clear();
        static_cast<void>(buildAiContext(tab, preferLastFailure));
        tab.aiLastPrompt = normalizedPrompt;
        tab.aiLastPreferFailure = preferLastFailure;
        tab.aiLastCommandRequest = commandRequest;
        tab.aiLastSelectedSkillIds = resolvedSkillIds;
        tab.aiLastWebSearchEnabled = webSearchEnabled;
    }
    auto messages = tab.aiConversation->providerMessagesWithEvidence(!appendPrompt);
    if (messages.empty())
    {
        static_cast<void>(tab.aiConversation->appendUserMessage(normalizedPrompt));
        messages = tab.aiConversation->providerMessagesWithEvidence();
    }
    if (!context.items.empty())
    {
        messages.insert(messages.end() - 1, ai::AiContextSerializer::asUntrustedEvidenceMessage(context));
    }
    tab.aiAssistantMessageId = tab.aiConversation->beginAssistantMessage();
    tab.aiUsage.reset();
    tab.aiError.clear();
    tab.aiProviderErrorCode.reset();
    tab.aiProviderRetryAfterMilliseconds.reset();
    tab.aiState = QStringLiteral("starting");
    tab.aiWebSearchQueries.clear();
    const QString tabId = tab.id;
    const auto assistantMessageId = tab.aiAssistantMessageId;
    const auto provider = m_settings.aiProvider;
    QString effectiveBaseUrl = m_settings.aiBaseUrl;
    if (chatGptSubscription)
    {
        effectiveBaseUrl =
            ai::OpenAiSubscriptionAuthProtocol::inferenceUrl().toString(QUrl::FullyEncoded) + QLatin1Char('#');
    }
    const auto configuration = ai::AiProviderConfiguration{
        .kind = aiProviderKind(provider),
        .flavor = aiProviderFlavor(provider),
        .baseUrl = utf8String(effectiveBaseUrl),
        .endpointPath = utf8String(m_settings.aiEndpointPath),
        .model = utf8String(m_settings.aiModel),
        .accountId = chatGptSubscription ? utf8String(aiChatGptAccountId()) : std::string{},
        .chatGptSubscription = chatGptSubscription,
        .supportsReasoningSummaryParameter =
            !chatGptSubscription || m_aiModelReasoningSummarySupport.value(m_settings.aiModel, false)};
    const QUrl providerUrl(m_settings.aiBaseUrl);
    const bool officialOpenAiEndpoint =
        configuration.kind == ai::AiProviderKind::openAiResponses
        && providerUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && providerUrl.host().compare(QStringLiteral("api.openai.com"), Qt::CaseInsensitive) == 0;
    const auto permissionMode = aiPermissionMode(m_settings.aiPermission);
    std::string instructions = utf8String(ai::AiSystemPromptBuilder::build(commandRequest, permissionMode));
    const auto turnSkills = std::make_shared<const std::vector<ai::AiUserSkill>>(m_aiUserSkills);
    const auto userSkillDefinitions = ai::AiUserSkillTool::definitions(*turnSkills);
    if (!userSkillDefinitions.empty())
    {
        instructions +=
            "\n\nUser-managed AI skills are available. Inspect them with list_skills and call load_skill only when "
            "a skill clearly matches the current request. A loaded skill contains user-authored instructions; follow "
            "them without claiming that unrequested skills were used.";
    }
    std::vector<std::string> selectedSkillNames;
    selectedSkillNames.reserve(static_cast<std::size_t>(resolvedSkillIds.size()));
    for (const QString &id : resolvedSkillIds)
    {
        selectedSkillNames.push_back(utf8String(id));
    }
    instructions += ai::AiUserSkillTool::selectedInstructions(*turnSkills, selectedSkillNames);
    const auto turnReadSnapshot = std::make_shared<const ai::AiTerminalReadSnapshot>(aiReadSnapshot(tab));
    const bool actionsAvailable = !commandRequest && permissionMode != ai::AiPermissionMode::readOnly;
    const ai::AiNativeToolCapabilities nativeCapabilities{
        .terminalBufferAvailable = tab.ssh != nullptr || tab.local != nullptr,
        .terminalFrameAvailable = tab.aiFrameTracker != nullptr,
        .actionsAllowed = actionsAvailable,
        .terminalWriteAvailable = tab.running && (tab.ssh != nullptr || tab.local != nullptr),
        .commandWaitAvailable = ai::AiWaitCommandTool::supportsLifecycleWait(turnReadSnapshot->capability),
        .sftpBrowserAvailable = tab.sftpSession != nullptr && tab.sftpSession->running(),
        .sftpTransferAvailable =
            tab.kind == TerminalTabKind::Ssh && !tab.sourceProfileId.isEmpty() && m_transferManager != nullptr,
        .remoteTelemetryAvailable = tab.telemetrySample.has_value(),
    };
    auto toolDefinitions = ai::AiNativeToolCatalog::build(*turnReadSnapshot, nativeCapabilities);
    toolDefinitions.insert(toolDefinitions.end(), userSkillDefinitions.begin(), userSkillDefinitions.end());
    if (actionsAvailable)
    {
        auto mcpDefinitions = m_mcpRuntime.definitions();
        toolDefinitions.insert(toolDefinitions.end(), std::make_move_iterator(mcpDefinitions.begin()),
                               std::make_move_iterator(mcpDefinitions.end()));
    }
    const auto turnTarget = std::make_shared<const ai::AiSessionTarget>(
        ai::AiSessionTarget{.sessionId = utf8String(tab.id), .sessionGeneration = tab.reconnectGeneration});
    ai::AiGenerationRequest generation{.instructions = std::move(instructions),
                                       .messages = std::move(messages),
                                       .tools = std::move(toolDefinitions),
                                       .reasoningEffort = aiReasoningEffort(m_settings.aiReasoning),
                                       .webSearchEnabled = webSearchEnabled};
    // Bound the request to the model context window before dispatch: a long
    // conversation is typed-compacted (old messages head/tail truncated,
    // recent turns preserved verbatim) instead of failing at the provider.
    // The conversation model itself is untouched; only the request view
    // changes, so follow-up turns re-compact deterministically.
    auto compacted = ai::AiContextCompactor::compact(std::move(generation));
    tab.aiCompaction = aiCompactionValue(compacted);
    if (compacted.compacted)
    {
        qCInfo(appControllerLog) << "AI context compacted for" << compacted.compactedItemCount << "item(s), removing"
                                 << compacted.removedBytes << "byte(s),"
                                 << (compacted.overBudget ? "still over budget" : "within budget");
    }
    generation = std::move(compacted.request);
    tab.aiTurnBudget = std::make_unique<ai::AiAgentTurnBudget>();
    tab.pendingAiAction.reset();
    tab.pendingAiMcpCall.reset();

    const auto started = startAiTurn(
        tab, configuration, std::move(generation),
        [this, provider]() -> std::expected<security::SensitiveByteArray, ai::AiProviderError> {
            if (provider == config::AiProviderPreference::ollama)
            {
                return security::SensitiveByteArray{};
            }
            const ai::AiSecretStore store(m_credentialVaults->active());
            if (provider == config::AiProviderPreference::openAiChatGpt)
            {
                auto tokens = store.readOAuthTokens(
                    aiCredentialReference(config::AiProviderPreference::openAiChatGpt).toStdString());
                if (!tokens.has_value())
                {
                    return std::unexpected(ai::AiProviderError{.code = ai::AiProviderErrorCode::authentication,
                                                               .message = "The ChatGPT sign-in is unavailable.",
                                                               .retryable = false});
                }
                return std::move(tokens->accessToken);
            }
            auto secret = store.readApiKey(m_settings.aiCredentialReference.toStdString());
            if (!secret.has_value())
            {
                return std::unexpected(ai::AiProviderError{
                    .code = ai::AiProviderErrorCode::authentication,
                    .message = "The configured AI API key is unavailable in the active credential vault.",
                    .retryable = false});
            }
            return std::move(secret.value());
        },
        [this, tabId, assistantMessageId](const auto, const ai::AiStreamEvent &event) {
            handleAiStreamEvent(tabId, assistantMessageId, event);
        },
        [this, tabId, assistantMessageId, providerKind = configuration.kind, model = configuration.model,
         officialOpenAiEndpoint, chatGptSubscription](const auto, const ai::AiTurnMetrics &metrics) {
            TerminalTab *target = findTab(tabId);
            if (target != nullptr)
            {
                const auto cost =
                    target->aiUsage.has_value()
                        ? ai::AiUsageEstimator::estimate(providerKind, model, *target->aiUsage, officialOpenAiEndpoint)
                        : ai::AiCostEstimate{};
                if (target->aiConversation && target->aiAssistantMessageId == assistantMessageId)
                {
                    static_cast<void>(target->aiConversation->setAssistantMetrics(assistantMessageId, metrics, cost));
                    persistAiConversation(*target);
                }
                if (target->aiState == QStringLiteral("starting") || target->aiState == QStringLiteral("retrying")
                    || target->aiState == QStringLiteral("streaming"))
                {
                    target->aiState = QStringLiteral("complete");
                }
            }
            if (m_activeTabId == tabId || m_focusedTabId == tabId)
            {
                emit aiConversationChanged();
            }
            if (chatGptSubscription)
            {
                refreshAiChatGptUsageInternal(true);
            }
        },
        [this, tabId](const auto, const auto, const auto) {
            TerminalTab *target = findTab(tabId);
            if (target != nullptr)
            {
                target->aiState = QStringLiteral("retrying");
            }
            if (m_activeTabId == tabId || m_focusedTabId == tabId)
            {
                emit aiConversationChanged();
            }
        },
        {},
        [this, tabId = tab.id, turnReadSnapshot, turnTarget, turnSkills](
            const ai::AiToolCall &call) -> std::expected<ai::AiTurnRunner::ToolHandlingResult, ai::AiProviderError> {
            auto *target = findTab(tabId);
            if ((call.name == "read_terminal_frame" || call.name == "wait_terminal_frame") && target != nullptr)
            {
                recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                auto handled = handleAiTerminalFrameTool(*target, tabId, call, *turnReadSnapshot);
                if (handled.output.has_value())
                {
                    const QString resultCode = aiActivityResultCode(handled.output->outputJson);
                    recordAiActivity(*target, call,
                                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                        : QStringLiteral("failed"),
                                     resultCode, false);
                }
                return handled;
            }
            if (call.name == "wait_command" && target != nullptr)
            {
                recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                auto handled = handleAiWaitCommand(*target, tabId, call, *turnTarget);
                if (handled.output.has_value())
                {
                    const QString resultCode = aiActivityResultCode(handled.output->outputJson);
                    recordAiActivity(*target, call,
                                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                        : QStringLiteral("failed"),
                                     resultCode, false);
                }
                return handled;
            }
            if (call.name == "list_sftp_path")
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget)
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{
                            .callId = call.id,
                            .name = call.name,
                            .outputJson = ai::AiSftpListTool::failure("session_unavailable",
                                                                      "The target terminal session is unavailable.")}};
                }
                const auto signature = call.name + ':' + call.argumentsJson;
                const auto budgetDecision =
                    target->aiTurnBudget->authorize(false, signature, target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiBudgetFailureJson(budgetDecision)}};
                }
                auto request = ai::AiSftpListTool::parse(call.argumentsJson, *turnTarget);
                if (!request.has_value())
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("invalid_arguments"),
                                     false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = std::move(request.error())}};
                }
                const ai::AiSessionTarget actualTarget{.sessionId = utf8String(target->id),
                                                       .sessionGeneration = target->reconnectGeneration};
                if (request->target != actualTarget)
                {
                    const auto output = ai::AiSftpListTool::failure(
                        "scope_changed", "The requested terminal session or generation is no longer active.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("scope_changed"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                if (!target->sftpSession || !target->sftpSession->running() || target->pendingAiSftpList.has_value())
                {
                    const auto output = ai::AiSftpListTool::failure(
                        "sftp_unavailable", "Open and connect the session's SFTP browser before listing a path.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("sftp_unavailable"),
                                     false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                const quint64 requestId = aiSftpListRequestFlag | ++target->aiSftpListRequestId;
                target->pendingAiSftpList = TerminalTab::PendingAiSftpList{.requestId = requestId,
                                                                           .call = call,
                                                                           .request = std::move(*request)};
                target->sftpSession->requestTreeDirectory(requestId, target->reconnectGeneration,
                                                          utf8QString(target->pendingAiSftpList->request.remotePath));
                return ai::AiTurnRunner::ToolHandlingResult{.cancel = [this, tabId, requestId] noexcept {
                    try
                    {
                        TerminalTab *cancelledTarget = findTab(tabId);
                        if (cancelledTarget == nullptr || !cancelledTarget->pendingAiSftpList.has_value()
                            || cancelledTarget->pendingAiSftpList->requestId != requestId)
                        {
                            return;
                        }
                        const auto cancelledCall = cancelledTarget->pendingAiSftpList->call;
                        cancelledTarget->pendingAiSftpList.reset();
                        recordAiActivity(*cancelledTarget, cancelledCall, QStringLiteral("cancelled"),
                                         QStringLiteral("cancelled"), false);
                    }
                    catch (...)
                    {
                        qCCritical(appControllerLog) << "AI SFTP listing cancellation suppressed an exception";
                    }
                }};
            }
            if (call.name == "read_sftp_file")
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget)
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{
                            .callId = call.id,
                            .name = call.name,
                            .outputJson = ai::AiSftpReadTool::failure("session_unavailable",
                                                                      "The target terminal session is unavailable.")}};
                }
                const auto signature = call.name + ':' + call.argumentsJson;
                const auto budgetDecision =
                    target->aiTurnBudget->authorize(false, signature, target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiBudgetFailureJson(budgetDecision)}};
                }
                auto request = ai::AiSftpReadTool::parse(call.argumentsJson, *turnTarget);
                if (!request.has_value())
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("invalid_arguments"),
                                     false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = std::move(request.error())}};
                }
                const ai::AiSessionTarget actualTarget{.sessionId = utf8String(target->id),
                                                       .sessionGeneration = target->reconnectGeneration};
                if (request->target != actualTarget)
                {
                    const auto output = ai::AiSftpReadTool::failure(
                        "scope_changed", "The requested terminal session or generation is no longer active.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("scope_changed"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                if (!target->sftpSession || !target->sftpSession->running() || target->pendingAiSftpRead.has_value())
                {
                    const auto output = ai::AiSftpReadTool::failure(
                        "sftp_unavailable", "Open and connect the session's SFTP browser before reading a file.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("sftp_unavailable"),
                                     false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                const quint64 requestId = ++target->aiSftpReadRequestId;
                target->pendingAiSftpRead = TerminalTab::PendingAiSftpRead{.requestId = requestId,
                                                                           .call = call,
                                                                           .request = std::move(*request)};
                target->sftpSession->requestReadFile(
                    requestId, target->reconnectGeneration, utf8QString(target->pendingAiSftpRead->request.remotePath),
                    static_cast<quint32>(target->pendingAiSftpRead->request.maximumBytes));
                return ai::AiTurnRunner::ToolHandlingResult{.cancel = [this, tabId, requestId] noexcept {
                    try
                    {
                        TerminalTab *cancelledTarget = findTab(tabId);
                        if (cancelledTarget == nullptr || !cancelledTarget->pendingAiSftpRead.has_value()
                            || cancelledTarget->pendingAiSftpRead->requestId != requestId)
                        {
                            return;
                        }
                        const auto call = cancelledTarget->pendingAiSftpRead->call;
                        if (cancelledTarget->sftpSession)
                        {
                            cancelledTarget->sftpSession->cancelReadFile(requestId);
                        }
                        cancelledTarget->pendingAiSftpRead.reset();
                        recordAiActivity(*cancelledTarget, call, QStringLiteral("cancelled"),
                                         QStringLiteral("cancelled"), false);
                    }
                    catch (...)
                    {
                        qCCritical(appControllerLog) << "AI SFTP read cancellation suppressed an exception";
                    }
                }};
            }
            if (call.name == "read_note")
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget)
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{
                            .callId = call.id,
                            .name = call.name,
                            .outputJson = ai::AiNoteReadTool::failure("session_unavailable",
                                                                      "The target terminal session is unavailable.")}};
                }
                const auto signature = call.name + ':' + call.argumentsJson;
                const auto budgetDecision =
                    target->aiTurnBudget->authorize(false, signature, target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiBudgetFailureJson(budgetDecision)}};
                }
                auto request = ai::AiNoteReadTool::parse(call.argumentsJson, *turnTarget);
                if (!request.has_value())
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("invalid_arguments"),
                                     false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = std::move(request.error())}};
                }
                const ai::AiSessionTarget actualTarget{.sessionId = utf8String(target->id),
                                                       .sessionGeneration = target->reconnectGeneration};
                if (request->target != actualTarget)
                {
                    const auto output = ai::AiNoteReadTool::failure(
                        "scope_changed", "The requested terminal session or generation is no longer active.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("scope_changed"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                if (target->pendingAiNoteRead.has_value())
                {
                    const auto output = ai::AiNoteReadTool::failure(
                        "busy", "Another note read is already active for this terminal assistant.");
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("busy"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                const quint64 requestId = ++target->aiNoteReadRequestId;
                target->pendingAiNoteRead = TerminalTab::PendingAiNoteRead{.requestId = requestId,
                                                                           .call = call,
                                                                           .request = std::move(*request)};
                const QString rootPath = m_noteStore.rootPath();
                const QString relativePath = target->pendingAiNoteRead->request.relativePath;
                const auto noteRequest =
                    std::make_shared<const ai::AiNoteReadRequest>(target->pendingAiNoteRead->request);
                const quint64 generation = target->reconnectGeneration;
                const QPointer<AppController> self(this);
                QThreadPool::globalInstance()->start(
                    [self, rootPath, relativePath, noteRequest, tabId, requestId, generation] {
                        std::string output;
                        try
                        {
                            auto read = workbench::NoteStore(rootPath).read(relativePath);
                            output = read.has_value() ? ai::AiNoteReadTool::result(*noteRequest, *read)
                                                      : ai::AiNoteReadTool::failure(read.error());
                        }
                        catch (const std::bad_alloc &)
                        {
                            output = ai::AiNoteReadTool::failure(workbench::NoteStoreError::io);
                        }
                        if (self)
                        {
                            emit self->aiNoteReadTaskCompleted(tabId, requestId, generation, relativePath,
                                                               QByteArray::fromStdString(output));
                        }
                    });
                return ai::AiTurnRunner::ToolHandlingResult{.cancel = [this, tabId, requestId] noexcept {
                    try
                    {
                        TerminalTab *cancelledTarget = findTab(tabId);
                        if (cancelledTarget == nullptr || !cancelledTarget->pendingAiNoteRead.has_value()
                            || cancelledTarget->pendingAiNoteRead->requestId != requestId)
                        {
                            return;
                        }
                        const auto cancelledCall = cancelledTarget->pendingAiNoteRead->call;
                        cancelledTarget->pendingAiNoteRead.reset();
                        recordAiActivity(*cancelledTarget, cancelledCall, QStringLiteral("cancelled"),
                                         QStringLiteral("cancelled"), false);
                    }
                    catch (...)
                    {
                        qCCritical(appControllerLog) << "AI note read cancellation suppressed an exception";
                    }
                }};
            }
            if (call.name == "read_terminal_output")
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget)
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{
                            .callId = call.id,
                            .name = call.name,
                            .outputJson = aiToolFailureJson(QStringLiteral("session_unavailable"),
                                                            tr("The target terminal session is unavailable."))}};
                }
                if (target->reconnectGeneration != turnTarget->sessionGeneration)
                {
                    const auto output = aiToolFailureJson(QStringLiteral("scope_changed"),
                                                          tr("The current terminal reconnected during this AI turn."));
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("scope_changed"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
                }
                const auto signature = call.name + ':' + call.argumentsJson;
                const auto budgetDecision =
                    target->aiTurnBudget->authorize(false, signature, target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiBudgetFailureJson(budgetDecision)}};
                }
                const auto output = executeAiScrollbackRead(*target, call);
                if (target != nullptr)
                {
                    const QString resultCode = aiActivityResultCode(output);
                    recordAiActivity(*target, call,
                                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                        : QStringLiteral("failed"),
                                     resultCode, false);
                }
                return ai::AiTurnRunner::ToolHandlingResult{
                    .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
            }
            if (const auto mcpTool = m_mcpRuntime.resolve(call.name); mcpTool.has_value())
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), true, true);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget
                    || target->pendingAiMcpCall.has_value())
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiToolFailureJson(
                                                       QStringLiteral("session_unavailable"),
                                                       tr("The MCP tool cannot be queued for this session."))},
                        .sideEffecting = true};
                }
                const ai::AiToolDispatchKey key{.conversationId = utf8String(target->aiConversationId),
                                                .turnId = activeAiTurnId(*target),
                                                .toolCallId = call.id};
                const auto admission = m_mcpDispatchLedger.begin({.key = key,
                                                                  .toolName = call.name,
                                                                  .canonicalArguments = call.argumentsJson,
                                                                  .sessionId = utf8String(target->id),
                                                                  .sessionGeneration = target->reconnectGeneration,
                                                                  .sideEffecting = true});
                if (admission.admission == ai::AiToolDispatchAdmission::cached && admission.record.has_value())
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = admission.record->resultJson},
                        .sideEffecting = true};
                }
                if (admission.admission != ai::AiToolDispatchAdmission::accepted)
                {
                    const QString code = admission.admission == ai::AiToolDispatchAdmission::joined
                                             ? QStringLiteral("duplicate_in_progress")
                                             : QStringLiteral("duplicate_or_invalid_call");
                    recordAiActivity(*target, call, QStringLiteral("failed"), code, true, true);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output =
                            ai::AiToolOutput{
                                .callId = call.id,
                                .name = call.name,
                                .outputJson = aiToolFailureJson(
                                    code,
                                    tr("The MCP tool call was duplicated, changed, or exceeded dispatch bounds."))},
                        .sideEffecting = true};
                }

                const auto permission = m_aiActionToolDispatcher.permissionDecision(
                    ai::AiPermissionCapability::mcpTool, call.name,
                    ai::AiActionToolContext{.conversationId = utf8String(target->aiConversationId),
                                            .turnId = activeAiTurnId(*target),
                                            .target = *turnTarget,
                                            .permissionMode = aiPermissionMode(m_settings.aiPermission),
                                            .profileId = utf8String(target->sourceProfileId),
                                            .writable = true},
                    true);
                if (permission.disposition == ai::AiPermissionDisposition::deny)
                {
                    const auto output =
                        aiToolFailureJson(QStringLiteral("permission_denied"),
                                          tr("The active mode or permission rule denied the MCP tool call."));
                    static_cast<void>(m_mcpDispatchLedger.transition(key, ai::AiToolDispatchState::failed, output));
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("permission_denied"), true,
                                     true);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output},
                        .sideEffecting = true};
                }
                const auto budgetDecision = target->aiTurnBudget->authorize(true, call.name + ':' + call.argumentsJson,
                                                                            target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    const auto output = aiBudgetFailureJson(budgetDecision);
                    static_cast<void>(m_mcpDispatchLedger.transition(key, ai::AiToolDispatchState::failed, output));
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), true,
                                     true);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output},
                        .sideEffecting = true};
                }
                const bool execute = permission.disposition == ai::AiPermissionDisposition::allow;
                if (!m_mcpDispatchLedger.transition(key, execute ? ai::AiToolDispatchState::running
                                                                 : ai::AiToolDispatchState::awaitingApproval))
                {
                    const auto output =
                        aiToolFailureJson(QStringLiteral("dispatch_state_changed"),
                                          tr("The MCP tool dispatch state changed before it could be started."));
                    static_cast<void>(m_mcpDispatchLedger.transition(key, ai::AiToolDispatchState::failed, output));
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("dispatch_state_changed"),
                                     true, true);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output},
                        .sideEffecting = true};
                }
                target->pendingAiMcpCall = TerminalTab::PendingAiMcpCall{.call = call,
                                                                         .dispatchKey = key,
                                                                         .tool = *mcpTool,
                                                                         .activeCall = std::nullopt};
                if (execute)
                {
                    recordAiActivity(*target, call, QStringLiteral("executing"), QStringLiteral("pending"), true, true);
                    const QString targetTabId = target->id;
                    const std::uint64_t targetGeneration = target->reconnectGeneration;
                    const auto callbackKey = std::make_shared<const ai::AiToolDispatchKey>(key);
                    const auto callbackCall = std::make_shared<const ai::AiToolCall>(call);
                    auto request = m_mcpRuntime.call(
                        call.name, call.argumentsJson,
                        [this, targetTabId, targetGeneration, callbackKey, callbackCall](auto result) noexcept {
                            try
                            {
                                completeAiMcpTool(targetTabId, targetGeneration, *callbackKey, *callbackCall,
                                                  std::move(result));
                            }
                            catch (...)
                            {
                                qCCritical(appControllerLog) << "Automatic MCP completion suppressed an exception";
                            }
                        });
                    if (!request.has_value())
                    {
                        const auto output = aiToolFailureJson(QStringLiteral("mcp_call_failed"), request.error());
                        static_cast<void>(m_mcpDispatchLedger.transition(key, ai::AiToolDispatchState::failed, output));
                        target->pendingAiMcpCall.reset();
                        recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("mcp_call_failed"),
                                         true, true);
                        return ai::AiTurnRunner::ToolHandlingResult{
                            .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output},
                            .sideEffecting = true};
                    }
                    if (target->pendingAiMcpCall.has_value() && target->pendingAiMcpCall->dispatchKey == key)
                    {
                        target->pendingAiMcpCall->activeCall = *request;
                    }
                }
                else
                {
                    recordAiActivity(*target, call, QStringLiteral("awaiting_approval"), QStringLiteral("pending"),
                                     true, true);
                }
                if (m_activeTabId == tabId || m_focusedTabId == tabId)
                {
                    emit aiConversationChanged();
                }
                const auto cancellationKey = std::make_shared<const ai::AiToolDispatchKey>(key);
                const auto cancellationCall = std::make_shared<const ai::AiToolCall>(call);
                return ai::AiTurnRunner::ToolHandlingResult{
                    .cancel =
                        [this, tabId, cancellationKey, cancellationCall] noexcept {
                            try
                            {
                                TerminalTab *cancelledTarget = findTab(tabId);
                                if (cancelledTarget == nullptr || !cancelledTarget->pendingAiMcpCall.has_value()
                                    || cancelledTarget->pendingAiMcpCall->dispatchKey != *cancellationKey)
                                {
                                    return;
                                }
                                if (cancelledTarget->pendingAiMcpCall->activeCall.has_value())
                                {
                                    static_cast<void>(m_mcpRuntime.cancel(
                                        *cancelledTarget->pendingAiMcpCall->activeCall, "The AI turn was cancelled."));
                                }
                                const auto output = aiToolFailureJson(QStringLiteral("cancelled"),
                                                                      tr("The MCP tool call was cancelled."));
                                static_cast<void>(m_mcpDispatchLedger.transition(
                                    *cancellationKey, ai::AiToolDispatchState::cancelled, output));
                                cancelledTarget->pendingAiMcpCall.reset();
                                recordAiActivity(*cancelledTarget, *cancellationCall, QStringLiteral("cancelled"),
                                                 QStringLiteral("cancelled"), true, true);
                                if (m_activeTabId == tabId || m_focusedTabId == tabId)
                                {
                                    emit aiConversationChanged();
                                }
                            }
                            catch (...)
                            {
                                qCCritical(appControllerLog)
                                    << "MCP tool cancellation suppressed an exception for tab" << tabId;
                            }
                        },
                    .sideEffecting = true};
            }
            if (call.name == "run_command" || call.name == "interrupt_command" || call.name == "write_to_pty"
                || call.name == "save_runbook" || call.name == "queue_sftp_download"
                || call.name == "queue_sftp_upload")
            {
                if (target != nullptr)
                {
                    recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), true);
                }
                if (target == nullptr || !aiTurnActive(*target) || !target->aiTurnBudget)
                {
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiToolFailureJson(
                                                       QStringLiteral("session_unavailable"),
                                                       tr("The target terminal session is unavailable."))},
                        .sideEffecting = true};
                }
                const auto plan = m_aiActionToolDispatcher.prepare(
                    call,
                    ai::AiActionToolContext{.conversationId = utf8String(target->aiConversationId),
                                            .turnId = activeAiTurnId(*target),
                                            .target = *turnTarget,
                                            .permissionMode = aiPermissionMode(m_settings.aiPermission),
                                            .profileId = utf8String(target->sourceProfileId),
                                            .writable =
                                                target->running && (target->ssh || target->local)
                                                && target->reconnectGeneration == turnTarget->sessionGeneration},
                    *target->aiTurnBudget);
                if (plan.disposition == ai::AiActionToolDisposition::respond || !plan.action.has_value())
                {
                    const QString resultCode = aiActivityResultCode(plan.outputJson);
                    recordAiActivity(*target, call,
                                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                        : QStringLiteral("failed"),
                                     resultCode, plan.sideEffecting);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = plan.outputJson},
                        .sideEffecting = plan.sideEffecting};
                }
                const auto action = *plan.action;
                if (plan.disposition == ai::AiActionToolDisposition::execute)
                {
                    recordAiActivity(*target, call, QStringLiteral("executing"), QStringLiteral("pending"), true,
                                     action.risk.highRisk());
                    if (action.kind == ai::AiTerminalActionKind::runCommand)
                    {
                        return handleAiRunCommand(*target, tabId, call, action);
                    }
                    const auto output = executeAiTerminalAction(*target, action);
                    static_cast<void>(
                        m_aiActionToolDispatcher.complete(action, ai::AiToolDispatchState::succeeded, output));
                    const QString resultCode = aiActivityResultCode(output);
                    recordAiActivity(*target, call,
                                     resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                        : QStringLiteral("failed"),
                                     resultCode, true, action.risk.highRisk());
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output},
                        .sideEffecting = true};
                }
                target->pendingAiAction = action;
                recordAiActivity(*target, call, QStringLiteral("awaiting_approval"), QStringLiteral("pending"), true,
                                 action.risk.highRisk());
                if (m_activeTabId == tabId || m_focusedTabId == tabId)
                {
                    emit aiConversationChanged();
                }
                return ai::AiTurnRunner::ToolHandlingResult{
                    .cancel =
                        [this, tabId, callId = call.id, toolName = call.name] noexcept {
                            try
                            {
                                TerminalTab *cancelledTarget = findTab(tabId);
                                if (cancelledTarget == nullptr || !cancelledTarget->pendingAiAction.has_value())
                                {
                                    return;
                                }
                                const auto cancelledAction = *cancelledTarget->pendingAiAction;
                                static_cast<void>(m_aiActionToolDispatcher.complete(
                                    cancelledAction, ai::AiToolDispatchState::cancelled,
                                    aiToolFailureJson(QStringLiteral("cancelled"),
                                                      tr("The pending command was cancelled."))));
                                const ai::AiToolCall cancelledCall{.id = callId, .name = toolName};
                                recordAiActivity(*cancelledTarget, cancelledCall, QStringLiteral("cancelled"),
                                                 QStringLiteral("cancelled"), true, cancelledAction.risk.highRisk());
                                cancelledTarget->pendingAiAction.reset();
                                if (m_activeTabId == tabId || m_focusedTabId == tabId)
                                {
                                    emit aiConversationChanged();
                                }
                            }
                            catch (...)
                            {
                                qCCritical(appControllerLog)
                                    << "AI tool cancellation suppressed an exception for tab" << tabId;
                            }
                        },
                    .sideEffecting = true};
            }
            if (target != nullptr && target->aiTurnBudget)
            {
                recordAiActivity(*target, call, QStringLiteral("queued"), QStringLiteral("pending"), false);
                const auto signature = call.name + ':' + call.argumentsJson;
                const auto budgetDecision =
                    target->aiTurnBudget->authorize(false, signature, target->reconnectGeneration);
                if (budgetDecision != ai::AiAgentBudgetDecision::allow)
                {
                    recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("budget_denied"), false);
                    return ai::AiTurnRunner::ToolHandlingResult{
                        .output = ai::AiToolOutput{.callId = call.id,
                                                   .name = call.name,
                                                   .outputJson = aiBudgetFailureJson(budgetDecision)}};
                }
            }
            const bool scopeChanged = target == nullptr || target->reconnectGeneration != turnTarget->sessionGeneration;
            const bool skillTool = call.name == "list_skills" || call.name == "load_skill";
            const auto output = scopeChanged ? aiToolFailureJson(QStringLiteral("scope_changed"),
                                                                 tr("The current terminal changed during the AI turn."))
                                : skillTool
                                    ? ai::AiUserSkillTool::execute(call.name, call.argumentsJson, *turnSkills)
                                    : m_aiReadToolDispatcher.execute(call.name, call.argumentsJson, *turnReadSnapshot);
            if (target != nullptr)
            {
                const QString resultCode = aiActivityResultCode(output);
                recordAiActivity(*target, call,
                                 resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                    : QStringLiteral("failed"),
                                 resultCode, false);
            }
            return ai::AiTurnRunner::ToolHandlingResult{
                .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output}};
        },
        [this, tabId](const ai::AiToolCall &call, const ai::AiToolOutput &output) {
            TerminalTab *target = findTab(tabId);
            if (target == nullptr || !target->aiConversation)
            {
                return;
            }
            if (target->aiAssistantMessageId != 0)
            {
                static_cast<void>(target->aiConversation->setAssistantToolDetails(
                    target->aiAssistantMessageId, utf8QString(call.id), aiToolDetailText(call.argumentsJson),
                    aiToolDetailText(output.outputJson)));
            }
            const QString evidence =
                QStringLiteral("[Assistant tool evidence]\nTool: %1\nArguments: %2\nResult: %3")
                    .arg(utf8QString(call.name), utf8QString(call.argumentsJson), utf8QString(output.outputJson));
            static_cast<void>(target->aiConversation->appendEvidenceMessage(evidence));
        },
        [this, tabId](const auto, const ai::AiCompactionResult &result) {
            TerminalTab *target = findTab(tabId);
            if (target == nullptr)
            {
                return;
            }
            target->aiCompaction = aiCompactionValue(result, target->aiCompaction);
            if (m_activeTabId == tabId || m_focusedTabId == tabId)
            {
                emit aiConversationChanged();
            }
        });
    if (!started.has_value())
    {
        tab.aiError = utf8QString(started.error().message);
        tab.aiProviderErrorCode = started.error().code;
        tab.aiProviderRetryAfterMilliseconds = started.error().retryAfterMilliseconds;
        static_cast<void>(tab.aiConversation->failAssistantMessage(assistantMessageId, tab.aiError));
        tab.aiState = QStringLiteral("error");
        emit aiConversationChanged();
        return false;
    }
    if (appendPrompt && !context.items.empty())
    {
        const auto serialized = ai::AiContextSerializer::serialize(context);
        static_cast<void>(tab.aiConversation->appendEvidenceMessageAfter(userMessageId, utf8QString(serialized.text)));
        tab.aiExplicitContextItems.clear();
        tab.aiExcludedContextIds.clear();
        tab.aiPinnedContextIds.clear();
        static_cast<void>(buildAiContext(tab, false));
    }
    emit aiConversationChanged();
    return true;
}

ai::AiTurnRunner::ToolHandlingResult
AppController::handleAiTerminalFrameTool(TerminalTab &owner, const QString &ownerTabId, const ai::AiToolCall &call,
                                         const ai::AiTerminalReadSnapshot &turnSnapshot)
{
    const ai::AiSessionTarget turnTarget{.sessionId = turnSnapshot.sessionId,
                                         .sessionGeneration = turnSnapshot.sessionGeneration};
    const bool waiting = call.name == "wait_terminal_frame";
    std::expected<ai::AiTerminalFrameWaitRequest, std::string> waitRequest =
        std::unexpected(ai::AiTerminalFrameTool::failure("invalid_arguments", "A frame wait was not requested."));
    std::expected<ai::AiTerminalFrameReadRequest, std::string> readRequest =
        std::unexpected(ai::AiTerminalFrameTool::failure("invalid_arguments", "A frame read was not requested."));
    if (waiting)
    {
        waitRequest = ai::AiTerminalFrameTool::parseWait(call.argumentsJson, turnTarget);
    }
    else
    {
        readRequest = ai::AiTerminalFrameTool::parseRead(call.argumentsJson, turnTarget);
    }
    if ((waiting && !waitRequest.has_value()) || (!waiting && !readRequest.has_value()))
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = waiting ? std::move(waitRequest.error())
                                                                 : std::move(readRequest.error())}};
    }
    const ai::AiSessionTarget requestTarget = waiting ? waitRequest->target : readRequest->target;
    const std::uint64_t afterRevision = waiting ? waitRequest->afterRevision : readRequest->afterRevision;
    if (requestTarget != turnTarget || utf8String(owner.id) != requestTarget.sessionId
        || owner.reconnectGeneration != requestTarget.sessionGeneration || !owner.aiFrameTracker)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = ai::AiTerminalFrameTool::failure(
                                               "scope_changed", "The current terminal changed during the AI turn.")}};
    }
    if (!owner.aiTurnBudget)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = ai::AiTerminalFrameTool::failure(
                                               "budget_unavailable", "The turn budget is unavailable.")}};
    }
    const auto decision =
        owner.aiTurnBudget->authorize(false, call.name + ':' + call.argumentsJson, owner.reconnectGeneration);
    if (decision != ai::AiAgentBudgetDecision::allow)
    {
        return {
            .output =
                ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = aiBudgetFailureJson(decision)}};
    }
    const ai::AiTerminalFrameDelta initial = owner.aiFrameTracker->snapshot(afterRevision);
    const std::string conversationId = utf8String(owner.aiConversationId);
    const std::string_view controlOwner = m_aiActionToolDispatcher.agentHasControl(requestTarget, conversationId)
                                              ? std::string_view{"current_agent"}
                                              : std::string_view{"unclaimed_or_other_agent"};
    const auto capability =
        ai::AiTerminalCapabilityAdapter::describe(turnSnapshot.shell, turnSnapshot.capability, initial.alternateScreen);
    if (!waiting)
    {
        return {.output =
                    ai::AiToolOutput{.callId = call.id,
                                     .name = call.name,
                                     .outputJson = ai::AiTerminalFrameTool::result(initial, controlOwner, capability)}};
    }
    if (ai::AiTerminalFrameTool::satisfied(*waitRequest, initial))
    {
        return {.output =
                    ai::AiToolOutput{.callId = call.id,
                                     .name = call.name,
                                     .outputJson = ai::AiTerminalFrameTool::result(initial, controlOwner, capability)}};
    }
    if (waitRequest->timeoutMilliseconds == 0)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson =
                                               ai::AiTerminalFrameTool::timeout(initial, controlOwner, capability)}};
    }

    auto *timer = new QTimer(this);
    timer->setInterval(50);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setProperty("ztermyAiRemainingMs", static_cast<qint64>(waitRequest->timeoutMilliseconds));
    const QPointer<QTimer> timerGuard(timer);
    const auto callGuard = std::make_shared<const ai::AiToolCall>(call);
    const auto requestGuard = std::make_shared<const ai::AiTerminalFrameWaitRequest>(*waitRequest);
    const auto shell = std::make_shared<const std::string>(turnSnapshot.shell);
    const auto semanticCapability = turnSnapshot.capability;
    QObject::connect(
        timer, &QTimer::timeout, this,
        [this, timerGuard, callGuard, requestGuard, ownerTabId, shell, semanticCapability] noexcept {
            try
            {
                if (!timerGuard)
                {
                    return;
                }
                TerminalTab *owner = findTab(ownerTabId);
                if (owner == nullptr || !aiTurnActive(*owner))
                {
                    timerGuard->stop();
                    timerGuard->deleteLater();
                    return;
                }
                if (utf8String(owner->id) != requestGuard->target.sessionId
                    || owner->reconnectGeneration != requestGuard->target.sessionGeneration || !owner->aiFrameTracker)
                {
                    timerGuard->stop();
                    timerGuard->deleteLater();
                    const auto output = ai::AiTerminalFrameTool::failure(
                        "scope_changed", "The terminal session changed while waiting for its frame.");
                    recordAiActivity(*owner, *callGuard, QStringLiteral("failed"), QStringLiteral("scope_changed"),
                                     false);
                    static_cast<void>(completePendingAiTool(
                        *owner,
                        ai::AiToolOutput{.callId = callGuard->id, .name = callGuard->name, .outputJson = output}));
                    return;
                }
                const ai::AiTerminalFrameDelta frame = owner->aiFrameTracker->snapshot(requestGuard->afterRevision);
                const std::string conversationId = utf8String(owner->aiConversationId);
                const std::string_view controlOwner =
                    m_aiActionToolDispatcher.agentHasControl(requestGuard->target, conversationId)
                        ? std::string_view{"current_agent"}
                        : std::string_view{"unclaimed_or_other_agent"};
                const auto capability =
                    ai::AiTerminalCapabilityAdapter::describe(*shell, semanticCapability, frame.alternateScreen);
                const qint64 remaining = std::max<qint64>(0, timerGuard->property("ztermyAiRemainingMs").toLongLong()
                                                                 - timerGuard->interval());
                timerGuard->setProperty("ztermyAiRemainingMs", remaining);
                if (!ai::AiTerminalFrameTool::satisfied(*requestGuard, frame) && remaining > 0)
                {
                    return;
                }
                timerGuard->stop();
                timerGuard->deleteLater();
                const auto output = ai::AiTerminalFrameTool::satisfied(*requestGuard, frame)
                                        ? ai::AiTerminalFrameTool::result(frame, controlOwner, capability)
                                        : ai::AiTerminalFrameTool::timeout(frame, controlOwner, capability);
                const QString code = aiActivityResultCode(output);
                recordAiActivity(*owner, *callGuard,
                                 code == QStringLiteral("ok") ? QStringLiteral("succeeded") : QStringLiteral("failed"),
                                 code, false);
                const bool resumed = completePendingAiTool(
                    *owner, ai::AiToolOutput{.callId = callGuard->id, .name = callGuard->name, .outputJson = output});
                if (!resumed)
                {
                    owner->aiError = tr("The terminal-frame wait result could not resume the AI turn.");
                    owner->aiState = QStringLiteral("error");
                    if (m_activeTabId == ownerTabId || m_focusedTabId == ownerTabId)
                    {
                        emit aiConversationChanged();
                    }
                }
            }
            catch (...)
            {
                if (timerGuard)
                {
                    timerGuard->stop();
                    timerGuard->deleteLater();
                }
                qCCritical(appControllerLog) << "AI terminal-frame wait suppressed an exception";
            }
        });
    timer->start();
    return {.cancel = [timerGuard] {
        if (timerGuard)
        {
            timerGuard->stop();
            timerGuard->deleteLater();
        }
    }};
}

ai::AiTurnRunner::ToolHandlingResult AppController::handleAiRunCommand(TerminalTab &tab, const QString &tabId,
                                                                       const ai::AiToolCall &call,
                                                                       const ai::AiTerminalAction &action)
{
    const std::string acceptedJson = executeAiRunCommand(tab, action);
    QJsonParseError parseError;
    const QJsonDocument acceptedDocument = QJsonDocument::fromJson(
        QByteArray(acceptedJson.data(), static_cast<qsizetype>(acceptedJson.size())), &parseError);
    const QJsonObject accepted = acceptedDocument.object();
    if (parseError.error != QJsonParseError::NoError || !acceptedDocument.isObject()
        || !accepted.value(QStringLiteral("ok")).toBool())
    {
        static_cast<void>(m_aiActionToolDispatcher.complete(action, ai::AiToolDispatchState::failed, acceptedJson));
        recordAiActivity(tab, call, QStringLiteral("failed"), aiActivityResultCode(acceptedJson), true,
                         action.risk.highRisk());
        return {.output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = acceptedJson},
                .sideEffecting = true};
    }

    const std::string commandId = utf8String(accepted.value(QStringLiteral("command_id")).toString());
    const bool lifecycleTracked = accepted.value(QStringLiteral("lifecycle_tracked")).toBool();
    const auto finishImmediately = [this, &tab, &call, &action](std::string output, const bool succeeded) {
        static_cast<void>(m_aiActionToolDispatcher.complete(
            action, succeeded ? ai::AiToolDispatchState::succeeded : ai::AiToolDispatchState::failed, output));
        recordAiActivity(tab, call, succeeded ? QStringLiteral("succeeded") : QStringLiteral("failed"),
                         aiActivityResultCode(output), true, action.risk.highRisk());
        return ai::AiTurnRunner::ToolHandlingResult{
            .output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = std::move(output)},
            .sideEffecting = true};
    };

    if (commandId.empty())
    {
        return finishImmediately(aiToolFailureJson(QStringLiteral("protocol_error"),
                                                   tr("The command dispatch did not return an identifier.")),
                                 false);
    }

    terminal::SemanticTerminalSnapshot semantic;
    if (tab.semanticObserver)
    {
        semantic = tab.semanticObserver->snapshot();
    }
    if (lifecycleTracked)
    {
        auto observed = m_aiCommandTracker.observe(commandId, semantic.commandBlocks, tab.running);
        if (!observed.has_value())
        {
            return finishImmediately(ai::AiWaitCommandTool::failure("command_not_found", "The command id is unknown."),
                                     false);
        }
        const bool completed = observed->state == ai::AiTrackedCommandState::finished
                               || observed->state == ai::AiTrackedCommandState::disconnected
                               || observed->state == ai::AiTrackedCommandState::outcomeUnknown;
        if (completed)
        {
            const auto output = ai::AiWaitCommandTool::result(*observed);
            return finishImmediately(output, aiActivityResultCode(output) == QStringLiteral("ok"));
        }
    }
    else if (!tab.aiFrameTracker)
    {
        QJsonObject unavailable = accepted;
        unavailable.insert(QStringLiteral("ok"), false);
        unavailable.insert(QStringLiteral("status"), QStringLiteral("outcome_unknown"));
        unavailable.insert(QStringLiteral("completion_confirmed"), false);
        unavailable.insert(QStringLiteral("error"),
                           QJsonObject{{QStringLiteral("code"), QStringLiteral("tracking_unavailable")},
                                       {QStringLiteral("message"),
                                        tr("The command was dispatched but no completion observer is available.")}});
        return finishImmediately(compactJson(unavailable), false);
    }

    auto *timer = new QTimer(this);
    timer->setInterval(lifecycleTracked ? 100 : 50);
    timer->setTimerType(lifecycleTracked ? Qt::CoarseTimer : Qt::PreciseTimer);
    timer->setProperty("ztermyAiRemainingMs", static_cast<qint64>(action.timeoutMilliseconds));
    timer->setProperty("ztermyAiFrameChanged", false);
    const QPointer<QTimer> timerGuard(timer);
    const auto callGuard = std::make_shared<const ai::AiToolCall>(call);
    const auto actionGuard = std::make_shared<const ai::AiTerminalAction>(action);
    const auto acceptedGuard = std::make_shared<const QJsonObject>(accepted);
    const auto commandIdGuard = std::make_shared<const std::string>(commandId);
    const auto turnSnapshot = aiReadSnapshot(tab);
    const auto shellGuard = std::make_shared<const std::string>(turnSnapshot.shell);
    const auto semanticCapability = turnSnapshot.capability;
    const std::uint64_t baselineRevision =
        static_cast<std::uint64_t>(accepted.value(QStringLiteral("frame_revision_before_dispatch")).toInteger());
    QObject::connect(
        timer, &QTimer::timeout, this,
        [this, timerGuard, callGuard, actionGuard, acceptedGuard, commandIdGuard, shellGuard, semanticCapability,
         baselineRevision, tabId, lifecycleTracked] noexcept {
            try
            {
                if (!timerGuard)
                {
                    return;
                }
                TerminalTab *target = findTab(tabId);
                if (target == nullptr || !aiTurnActive(*target))
                {
                    timerGuard->stop();
                    timerGuard->deleteLater();
                    return;
                }
                const qint64 remaining = std::max<qint64>(0, timerGuard->property("ztermyAiRemainingMs").toLongLong()
                                                                 - timerGuard->interval());
                timerGuard->setProperty("ztermyAiRemainingMs", remaining);

                std::string output;
                bool finished = false;
                bool succeeded = false;
                if (lifecycleTracked)
                {
                    terminal::SemanticTerminalSnapshot current;
                    if (target->semanticObserver)
                    {
                        current = target->semanticObserver->snapshot();
                    }
                    const auto observed =
                        m_aiCommandTracker.observe(*commandIdGuard, current.commandBlocks, target->running);
                    if (!observed.has_value())
                    {
                        output = ai::AiWaitCommandTool::failure("command_not_found", "The command id is unknown.");
                        finished = true;
                    }
                    else
                    {
                        const bool terminalState = observed->state == ai::AiTrackedCommandState::finished
                                                   || observed->state == ai::AiTrackedCommandState::disconnected
                                                   || observed->state == ai::AiTrackedCommandState::outcomeUnknown;
                        if (terminalState)
                        {
                            output = ai::AiWaitCommandTool::result(*observed);
                            finished = true;
                            succeeded = aiActivityResultCode(output) == QStringLiteral("ok");
                        }
                        else if (remaining == 0)
                        {
                            output = ai::AiWaitCommandTool::timeout(*observed);
                            finished = true;
                        }
                    }
                }
                else
                {
                    if (!target->aiFrameTracker)
                    {
                        output = aiToolFailureJson(QStringLiteral("scope_changed"),
                                                   tr("The terminal frame observer is unavailable."));
                        finished = true;
                    }
                    else
                    {
                        const ai::AiTerminalFrameDelta frame = target->aiFrameTracker->snapshot(baselineRevision);
                        const bool changed =
                            timerGuard->property("ztermyAiFrameChanged").toBool() || frame.revision > baselineRevision;
                        timerGuard->setProperty("ztermyAiFrameChanged", changed);
                        const bool idle = changed && frame.idleMilliseconds >= 750;
                        if (idle || remaining == 0)
                        {
                            const ai::AiSessionTarget currentTarget{.sessionId = utf8String(target->id),
                                                                    .sessionGeneration = target->reconnectGeneration};
                            const std::string conversationId = utf8String(target->aiConversationId);
                            const std::string_view controlOwner =
                                m_aiActionToolDispatcher.agentHasControl(currentTarget, conversationId)
                                    ? std::string_view{"current_agent"}
                                    : std::string_view{"unclaimed_or_other_agent"};
                            const auto capability = ai::AiTerminalCapabilityAdapter::describe(
                                *shellGuard, semanticCapability, frame.alternateScreen);
                            const auto frameJson =
                                idle ? ai::AiTerminalFrameTool::result(frame, controlOwner, capability)
                                     : ai::AiTerminalFrameTool::timeout(frame, controlOwner, capability);
                            output = aiRunCommandFrameResult(*acceptedGuard, frameJson, !idle);
                            finished = true;
                            succeeded = idle;
                        }
                    }
                }
                if (!finished)
                {
                    return;
                }

                timerGuard->stop();
                timerGuard->deleteLater();
                static_cast<void>(m_aiActionToolDispatcher.complete(
                    *actionGuard, succeeded ? ai::AiToolDispatchState::succeeded : ai::AiToolDispatchState::failed,
                    output));
                recordAiActivity(*target, *callGuard,
                                 succeeded ? QStringLiteral("succeeded") : QStringLiteral("failed"),
                                 aiActivityResultCode(output), true, actionGuard->risk.highRisk());
                if (!completePendingAiTool(
                        *target,
                        ai::AiToolOutput{.callId = callGuard->id, .name = callGuard->name, .outputJson = output}))
                {
                    target->aiError = tr("The command result could not resume the AI turn.");
                    target->aiState = QStringLiteral("error");
                    if (m_activeTabId == tabId || m_focusedTabId == tabId)
                    {
                        emit aiConversationChanged();
                    }
                }
            }
            catch (...)
            {
                if (timerGuard)
                {
                    timerGuard->stop();
                    timerGuard->deleteLater();
                }
                qCCritical(appControllerLog) << "AI command execution wait suppressed an exception";
            }
        });
    timer->start();
    return {.cancel =
                [this, timerGuard, tabId, callGuard, actionGuard] noexcept {
                    if (timerGuard)
                    {
                        timerGuard->stop();
                        timerGuard->deleteLater();
                    }
                    try
                    {
                        static_cast<void>(m_aiActionToolDispatcher.complete(
                            *actionGuard, ai::AiToolDispatchState::cancelled,
                            aiToolFailureJson(QStringLiteral("cancelled"), tr("The command wait was cancelled."))));
                        if (TerminalTab *target = findTab(tabId); target != nullptr)
                        {
                            recordAiActivity(*target, *callGuard, QStringLiteral("cancelled"),
                                             QStringLiteral("cancelled"), true, actionGuard->risk.highRisk());
                        }
                    }
                    catch (...)
                    {
                        qCCritical(appControllerLog) << "AI command execution cancellation suppressed an exception";
                    }
                },
            .sideEffecting = true};
}

ai::AiTurnRunner::ToolHandlingResult AppController::handleAiWaitCommand(TerminalTab &tab, const QString &tabId,
                                                                        const ai::AiToolCall &call,
                                                                        const ai::AiSessionTarget &turnTarget)
{
    const auto request = ai::AiWaitCommandTool::parse(call.argumentsJson, turnTarget);
    if (!request.has_value())
    {
        return {.output = ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = request.error()}};
    }
    const auto tracked = m_aiCommandTracker.find(request->commandId);
    if (!tracked.has_value())
    {
        return {.output = ai::AiToolOutput{
                    .callId = call.id,
                    .name = call.name,
                    .outputJson = ai::AiWaitCommandTool::failure("command_not_found", "The command id is unknown.")}};
    }
    if (tracked->conversationId != utf8String(tab.aiConversationId) || tracked->target != request->target
        || request->target.sessionId != utf8String(tab.id)
        || request->target.sessionGeneration != tab.reconnectGeneration)
    {
        return {.output = ai::AiToolOutput{
                    .callId = call.id,
                    .name = call.name,
                    .outputJson = ai::AiWaitCommandTool::failure("scope_changed", "The command scope changed.")}};
    }
    if (!tab.aiTurnBudget)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = ai::AiWaitCommandTool::failure(
                                               "budget_unavailable", "The turn budget is unavailable.")}};
    }
    const auto budgetDecision = tab.aiTurnBudget->authorize(false, std::string("wait_command:") + request->commandId,
                                                            request->target.sessionGeneration);
    if (budgetDecision != ai::AiAgentBudgetDecision::allow)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = aiBudgetFailureJson(budgetDecision)}};
    }

    terminal::SemanticTerminalSnapshot semantic;
    if (tab.semanticObserver)
    {
        semantic = tab.semanticObserver->snapshot();
    }
    if (!ai::AiWaitCommandTool::supportsLifecycleWait(semanticCapability(semantic)))
    {
        return {.output = ai::AiToolOutput{
                    .callId = call.id,
                    .name = call.name,
                    .outputJson = ai::AiWaitCommandTool::failure(
                        "unsupported", "Semantic command lifecycle is unavailable; wait on the terminal frame.")}};
    }
    auto observed = m_aiCommandTracker.observe(request->commandId, semantic.commandBlocks, tab.running);
    if (!observed.has_value())
    {
        return {.output = ai::AiToolOutput{
                    .callId = call.id,
                    .name = call.name,
                    .outputJson = ai::AiWaitCommandTool::failure("command_not_found", "The command id is unknown.")}};
    }
    const bool terminalState = observed->state == ai::AiTrackedCommandState::finished
                               || observed->state == ai::AiTrackedCommandState::disconnected
                               || observed->state == ai::AiTrackedCommandState::outcomeUnknown;
    if (terminalState)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = ai::AiWaitCommandTool::result(*observed)}};
    }
    if (request->timeoutMilliseconds == 0)
    {
        return {.output = ai::AiToolOutput{.callId = call.id,
                                           .name = call.name,
                                           .outputJson = ai::AiWaitCommandTool::timeout(*observed)}};
    }

    auto *timer = new QTimer(this);
    timer->setInterval(100);
    timer->setTimerType(Qt::CoarseTimer);
    timer->setProperty("ztermyAiTabId", tabId);
    timer->setProperty("ztermyAiToolCallId", utf8QString(call.id));
    timer->setProperty("ztermyAiToolName", utf8QString(call.name));
    timer->setProperty("ztermyAiCommandId", utf8QString(request->commandId));
    timer->setProperty("ztermyAiRemainingMs", static_cast<qint64>(request->timeoutMilliseconds));
    const QPointer<QTimer> timerGuard(timer);
    const auto resumeWithOutput = [this](TerminalTab *target, const ai::AiToolCall &call, const QString &tabId,
                                         const std::string &output) {
        // A rejected tool output (size/identity mismatch) would otherwise
        // leave the turn waiting forever; surface it as a visible failure.
        const bool resumed = completePendingAiTool(
            *target, ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = output});
        if (!resumed)
        {
            target->aiError = tr("The command-wait result could not resume the AI turn.");
            target->aiState = QStringLiteral("error");
            if (m_activeTabId == tabId || m_focusedTabId == tabId)
            {
                emit aiConversationChanged();
            }
        }
    };
    QObject::connect(timer, &QTimer::timeout, this, [this, timerGuard, resumeWithOutput] noexcept {
        try
        {
            if (!timerGuard)
            {
                return;
            }
            const QString tabId = timerGuard->property("ztermyAiTabId").toString();
            const ai::AiToolCall call{.id = utf8String(timerGuard->property("ztermyAiToolCallId").toString()),
                                      .name = utf8String(timerGuard->property("ztermyAiToolName").toString())};
            const auto commandId = utf8String(timerGuard->property("ztermyAiCommandId").toString());
            TerminalTab *target = findTab(tabId);
            if (target == nullptr || !aiTurnActive(*target))
            {
                timerGuard->stop();
                timerGuard->deleteLater();
                return;
            }
            terminal::SemanticTerminalSnapshot current;
            if (target->semanticObserver)
            {
                current = target->semanticObserver->snapshot();
            }
            const auto currentCommand = m_aiCommandTracker.observe(commandId, current.commandBlocks, target->running);
            if (!currentCommand.has_value())
            {
                timerGuard->stop();
                timerGuard->deleteLater();
                recordAiActivity(*target, call, QStringLiteral("failed"), QStringLiteral("command_not_found"), false);
                resumeWithOutput(target, call, tabId,
                                 ai::AiWaitCommandTool::failure("command_not_found", "The command id is unknown."));
                return;
            }
            const bool completed = currentCommand->state == ai::AiTrackedCommandState::finished
                                   || currentCommand->state == ai::AiTrackedCommandState::disconnected
                                   || currentCommand->state == ai::AiTrackedCommandState::outcomeUnknown;
            const qint64 remaining =
                std::max<qint64>(0, timerGuard->property("ztermyAiRemainingMs").toLongLong() - timerGuard->interval());
            timerGuard->setProperty("ztermyAiRemainingMs", remaining);
            if (!completed && remaining > 0)
            {
                return;
            }
            timerGuard->stop();
            timerGuard->deleteLater();
            const auto output = completed ? ai::AiWaitCommandTool::result(*currentCommand)
                                          : ai::AiWaitCommandTool::timeout(*currentCommand);
            const QString resultCode = aiActivityResultCode(output);
            recordAiActivity(*target, call,
                             resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                : QStringLiteral("failed"),
                             resultCode, false);
            resumeWithOutput(target, call, tabId, output);
        }
        catch (...)
        {
            if (timerGuard)
            {
                timerGuard->stop();
                timerGuard->deleteLater();
            }
            qCCritical(appControllerLog) << "AI command wait suppressed an exception";
        }
    });
    timer->start();
    const auto callGuard = std::make_shared<const ai::AiToolCall>(call);
    return {.cancel = [this, timerGuard, tabId, callGuard] {
        if (timerGuard)
        {
            timerGuard->stop();
            timerGuard->deleteLater();
        }
        try
        {
            if (TerminalTab *target = findTab(tabId); target != nullptr)
            {
                recordAiActivity(*target, *callGuard, QStringLiteral("cancelled"), QStringLiteral("cancelled"), false);
            }
        }
        catch (...)
        {
            qCCritical(appControllerLog) << "AI wait cancellation suppressed an exception for tab" << tabId;
        }
    }};
}

std::string AppController::executeAiScrollbackRead(TerminalTab &tab, const ai::AiToolCall &call)
{
    const ai::AiSessionTarget target{.sessionId = utf8String(tab.id), .sessionGeneration = tab.reconnectGeneration};
    const auto request = ai::AiTerminalOutputTool::parse(call.argumentsJson, target);
    if (!request.has_value())
    {
        return request.error();
    }

    std::expected<terminal::TerminalScrollbackPage, std::error_code> page =
        std::unexpected(std::make_error_code(std::errc::not_connected));
    if (tab.ssh != nullptr)
    {
        page = tab.ssh->scrollbackPage(
            {.anchor = request->anchor, .offset = request->offset, .lineCount = request->lineCount});
    }
    else if (tab.local != nullptr)
    {
        page = tab.local->scrollbackPage(
            {.anchor = request->anchor, .offset = request->offset, .lineCount = request->lineCount});
    }
    if (!page)
    {
        return ai::AiTerminalOutputTool::failure(
            "session_unavailable", utf8String(tr("The terminal scrollback is unavailable for this session: %1.")
                                                  .arg(QString::fromStdString(page.error().message()))));
    }
    return ai::AiTerminalOutputTool::result(*request, *page);
}

std::string AppController::executeAiTerminalAction(TerminalTab &tab, const ai::AiTerminalAction &action)
{
    switch (action.kind)
    {
        case ai::AiTerminalActionKind::runCommand:
            return executeAiRunCommand(tab, action);
        case ai::AiTerminalActionKind::writeToPty:
            return executeAiWriteToPty(tab, action);
        case ai::AiTerminalActionKind::interruptCommand:
            return executeAiInterruptCommand(tab, action);
        case ai::AiTerminalActionKind::saveRunbook:
            return executeAiSaveRunbook(action);
        case ai::AiTerminalActionKind::enqueueSftpDownload:
        case ai::AiTerminalActionKind::enqueueSftpUpload:
            return executeAiSftpTransfer(tab, action);
    }
    return aiToolFailureJson(QStringLiteral("unsupported"), tr("The terminal action is unsupported."));
}

std::string AppController::executeAiSaveRunbook(const ai::AiTerminalAction &action)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(action.payloadJson), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return aiToolFailureJson(QStringLiteral("invalid_arguments"), tr("The proposed runbook is invalid."));
    }

    const QJsonObject runbook = document.object();
    QVariantList steps;
    const QJsonArray runbookSteps = runbook.value(QStringLiteral("steps")).toArray();
    steps.reserve(runbookSteps.size());
    for (const auto &stepValue : runbookSteps)
    {
        const QJsonObject step = stepValue.toObject();
        steps.append(QVariantMap{
            {QStringLiteral("command"), step.value(QStringLiteral("command")).toString()},
            {QStringLiteral("continuation"), step.value(QStringLiteral("continuation")).toString()},
            {QStringLiteral("outputMarker"), step.value(QStringLiteral("output_marker")).toString()},
            {QStringLiteral("timeoutMs"),
             QVariant::fromValue<qulonglong>(step.value(QStringLiteral("timeout_ms")).toVariant().toULongLong())}});
    }

    const std::size_t previousCount = m_scripts.size();
    if (!saveScript(
            QVariantMap{{QStringLiteral("name"), runbook.value(QStringLiteral("name")).toString()},
                        {QStringLiteral("description"), runbook.value(QStringLiteral("description")).toString()},
                        {QStringLiteral("shell"), runbook.value(QStringLiteral("shell")).toString()},
                        {QStringLiteral("variables"), QVariantList{}},
                        {QStringLiteral("steps"), steps}}))
    {
        const QString detail = m_quickCommandOperationError.isEmpty() ? tr("The runbook could not be saved.")
                                                                      : m_quickCommandOperationError;
        return aiToolFailureJson(QStringLiteral("save_failed"), detail);
    }
    if (m_scripts.size() <= previousCount)
    {
        return aiToolFailureJson(QStringLiteral("save_failed"), tr("The runbook could not be saved."));
    }

    return compactJson(QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("status"), QStringLiteral("saved")},
                                   {QStringLiteral("runbook_id"), utf8QString(m_scripts.back().id)},
                                   {QStringLiteral("name"), runbook.value(QStringLiteral("name"))},
                                   {QStringLiteral("step_count"), runbookSteps.size()}});
}

std::string AppController::executeAiSftpTransfer(TerminalTab &tab, const ai::AiTerminalAction &action)
{
    if (tab.kind != TerminalTabKind::Ssh || tab.sourceProfileId.isEmpty() || m_transferManager == nullptr)
    {
        return aiToolFailureJson(QStringLiteral("sftp_unavailable"), tr("SFTP transfer is unavailable."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(action.payloadJson), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return aiToolFailureJson(QStringLiteral("invalid_arguments"), tr("The SFTP transfer paths are invalid."));
    }
    const QJsonObject payload = document.object();
    const QString localPath = QDir::cleanPath(payload.value(QStringLiteral("local_path")).toString());
    const auto remotePath =
        sftp::normalizeRemotePath(utf8String(payload.value(QStringLiteral("remote_path")).toString()));
    if (!QDir::isAbsolutePath(localPath) || !remotePath || *remotePath == "/")
    {
        return aiToolFailureJson(QStringLiteral("invalid_path"), tr("The SFTP transfer paths are invalid."));
    }

    const bool upload = action.kind == ai::AiTerminalActionKind::enqueueSftpUpload;
    const QFileInfo localFile(localPath);
    if (upload && (!localFile.exists() || !localFile.isFile() || localFile.isSymLink()))
    {
        return aiToolFailureJson(QStringLiteral("invalid_source"),
                                 tr("The local upload source must be a regular non-symlink file."));
    }
    if (!upload
        && (localFile.isSymLink() || (localFile.exists() && localFile.isDir()) || !localFile.absoluteDir().exists()))
    {
        return aiToolFailureJson(
            QStringLiteral("invalid_destination"),
            tr("The local download destination must be a non-symlink file path in an existing directory."));
    }

    const std::string taskId = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    sftp::TransferTask task{
        .id = taskId,
        .endpointId = utf8String(tab.sourceProfileId),
        .displayName = utf8String(upload ? localFile.fileName() : QFileInfo(utf8QString(*remotePath)).fileName()),
        .sourcePath = upload ? utf8String(localFile.absoluteFilePath()) : *remotePath,
        .destinationPath = upload ? *remotePath : utf8String(localFile.absoluteFilePath()),
        .filenameEncoding = utf8String(tab.sftpFilenameEncoding),
        .direction = upload ? sftp::TransferDirection::Upload : sftp::TransferDirection::Download,
        .totalBytes = upload ? static_cast<std::uint64_t>(localFile.size()) : 0,
        .sourceModifiedUtcSeconds =
            upload ? std::optional<std::int64_t>{localFile.lastModified().toSecsSinceEpoch()} : std::nullopt};
    const auto queued = m_transferManager->enqueue(std::move(task), transferRequestProvider(tab.sourceProfileId), {});
    if (!queued)
    {
        return aiToolFailureJson(QStringLiteral("queue_failed"), tr("The SFTP transfer could not be queued."));
    }
    return compactJson(
        QJsonObject{{QStringLiteral("ok"), true},
                    {QStringLiteral("status"), QStringLiteral("queued")},
                    {QStringLiteral("transfer_id"), utf8QString(taskId)},
                    {QStringLiteral("direction"), upload ? QStringLiteral("upload") : QStringLiteral("download")}});
}

std::string AppController::executeAiWriteToPty(TerminalTab &tab, const ai::AiTerminalAction &action)
{
    if (aiUserHasPendingLine(tab))
    {
        return aiToolFailureJson(QStringLiteral("user_input_pending"),
                                 tr("The user is typing a command line; wait for it to finish."));
    }
    QByteArray bytes(action.ptyData.data(), static_cast<qsizetype>(action.ptyData.size()));
    if (action.appendEnter)
    {
        bytes.append('\r');
    }
    dispatchInput(tab, bytes);
    return compactJson(QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("status"), QStringLiteral("accepted")},
                                   {QStringLiteral("bytes_written"), static_cast<qint64>(bytes.size())},
                                   {QStringLiteral("enter_appended"), action.appendEnter},
                                   {QStringLiteral("content_echoed"), false}});
}

std::string AppController::executeAiRunCommand(TerminalTab &tab, const ai::AiTerminalAction &action)
{
    if (aiUserHasPendingLine(tab))
    {
        return aiToolFailureJson(QStringLiteral("user_input_pending"),
                                 tr("The user is typing a command line; wait for it to finish."));
    }
    terminal::CommandBlockId baselineBlockId = 0;
    terminal::TerminalSemanticCapability capability = terminal::TerminalSemanticCapability::none;
    if (tab.semanticObserver)
    {
        const auto before = tab.semanticObserver->snapshot();
        capability = semanticCapability(before);
        if (!before.commandBlocks.empty())
        {
            baselineBlockId = before.commandBlocks.back().id;
        }
    }
    const std::uint64_t frameRevision = tab.aiFrameTracker ? tab.aiFrameTracker->snapshot(0).revision : 0;
    const auto commandId = aiCommandId();
    const bool tracked =
        m_aiCommandTracker.accept(ai::AiTrackedCommand{.id = commandId,
                                                       .conversationId = action.dispatchKey.conversationId,
                                                       .target = action.target,
                                                       .command = action.command,
                                                       .baselineBlockId = baselineBlockId});
    QByteArray bytes(action.command.data(), static_cast<qsizetype>(action.command.size()));
    bytes.replace("\r\n", "\r");
    bytes.replace('\n', '\r');
    if (bytes.isEmpty() || bytes.back() != '\r')
    {
        bytes.append('\r');
    }
    // Synthetic echo: announce the command as a shell comment line so the
    // user sees the agent's action in the terminal before it executes. The
    // marker is dispatched directly (never fed to observeTerminalInput) so it
    // cannot pollute the user input reconstruction or the command history.
    const QString shell = tab.kind == TerminalTabKind::Local ? QStringLiteral("pwsh") : QString{};
    const std::string marker = ai::AiCommandEcho::markerLine(action.command, utf8String(shell));
    QByteArray markerBytes(marker.data(), static_cast<qsizetype>(marker.size()));
    markerBytes.append('\r');
    dispatchInput(tab, markerBytes);
    observeTerminalInput(tab, bytes);
    dispatchInput(tab, bytes);
    return ai::AiWaitCommandTool::accepted(commandId, tracked, capability, frameRevision);
}

std::string AppController::executeAiInterruptCommand(TerminalTab &tab, const ai::AiTerminalAction &action)
{
    const auto tracked = m_aiCommandTracker.find(action.commandId);
    if (!tracked.has_value())
    {
        return aiToolFailureJson(QStringLiteral("command_not_found"), tr("The tracked command no longer exists."));
    }
    if (tracked->conversationId != action.dispatchKey.conversationId || tracked->target != action.target)
    {
        return aiToolFailureJson(QStringLiteral("scope_changed"), tr("The tracked command scope changed."));
    }
    terminal::SemanticTerminalSnapshot semantic;
    if (tab.semanticObserver)
    {
        semantic = tab.semanticObserver->snapshot();
    }
    const auto current = m_aiCommandTracker.observe(action.commandId, semantic.commandBlocks, tab.running);
    if (!current.has_value())
    {
        return aiToolFailureJson(QStringLiteral("command_not_found"), tr("The tracked command no longer exists."));
    }
    if (current->state == ai::AiTrackedCommandState::finished
        || current->state == ai::AiTrackedCommandState::disconnected
        || current->state == ai::AiTrackedCommandState::outcomeUnknown)
    {
        return aiToolFailureJson(QStringLiteral("command_not_running"),
                                 tr("The tracked command is no longer running."));
    }
    if (!m_aiCommandTracker.markInterruptRequested(action.commandId))
    {
        return aiToolFailureJson(QStringLiteral("interrupt_unavailable"),
                                 tr("The tracked command could not be interrupted."));
    }
    // Announce the interrupt in the terminal before sending Ctrl+C.
    const std::string marker = ai::AiCommandEcho::markerLine(
        "<interrupt>", tab.kind == TerminalTabKind::Local ? std::string_view{"pwsh"} : std::string_view{});
    QByteArray markerBytes(marker.data(), static_cast<qsizetype>(marker.size()));
    markerBytes.append('\x03');
    observeTerminalInput(tab, markerBytes);
    dispatchInput(tab, markerBytes);
    return compactJson(QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("status"), QStringLiteral("interrupt_requested")},
                                   {QStringLiteral("command_id"), utf8QString(action.commandId)},
                                   {QStringLiteral("mode"), QStringLiteral("soft")},
                                   {QStringLiteral("effect"), QStringLiteral("ctrl_c_written_to_pty")},
                                   {QStringLiteral("outcome_confirmed"), false}});
}

bool AppController::resetApplicationSettings()
{
    config::ApplicationSettings defaults;
    defaults.credentialStorage = m_settings.credentialStorage;
    if (!persistApplicationSettings(defaults))
    {
        return false;
    }
    m_actionRegistry.setOverrides(m_settings.shortcutOverrides);
    emit actionRegistryChanged();
    return true;
}

bool AppController::initializePortableCredentialVault(const QString &masterPassword)
{
    auto initialized = m_credentialVaults->initializePortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!initialized)
    {
        setCredentialOperationError(credentialVaultErrorMessage(initialized.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    applyAiNetworkProxy();
    if (m_aiConversationHistory && m_settings.aiConversationHistoryEnabled
        && m_credentialVaults->storage() == security::CredentialStorage::Portable)
    {
        m_aiConversationHistory->setVault(m_credentialVaults->active());
        m_aiConversationHistory->reload();
    }
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        if (rule.autoStart && findPortForwardingRuntime(rule.id) == nullptr)
        {
            (void)startPortForwardingRule(utf8QString(rule.id));
        }
    }
    return true;
}

bool AppController::unlockPortableCredentialVault(const QString &masterPassword)
{
    auto unlocked = m_credentialVaults->unlockPortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!unlocked)
    {
        setCredentialOperationError(credentialVaultErrorMessage(unlocked.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    applyAiNetworkProxy();
    if (m_aiConversationHistory && m_settings.aiConversationHistoryEnabled
        && m_credentialVaults->storage() == security::CredentialStorage::Portable)
    {
        m_aiConversationHistory->setVault(m_credentialVaults->active());
        m_aiConversationHistory->reload();
    }
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        if (rule.autoStart && findPortForwardingRuntime(rule.id) == nullptr)
        {
            (void)startPortForwardingRule(utf8QString(rule.id));
        }
    }
    return true;
}

bool AppController::changePortableVaultMasterPassword(const QString &masterPassword)
{
    auto changed =
        m_credentialVaults->changePortableMasterPassword(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!changed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(changed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

void AppController::lockPortableCredentialVault()
{
    if (m_aiConversationHistory)
    {
        m_aiConversationHistory->setVault(m_credentialVaults->active());
        m_aiConversationHistory->forgetLoaded();
    }
    m_credentialVaults->lockPortable();
    applyAiNetworkProxy();
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
}

bool AppController::migrateCredentialStorage(const QString &target, const bool removeSource)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    const security::CredentialStorage previousStorage = m_credentialVaults->storage();
    if (*parsedTarget == previousStorage)
    {
        setCredentialOperationError({});
        return true;
    }
    if (*parsedTarget == security::CredentialStorage::Session && removeSource
        && QFileInfo::exists(siblingAiConversationHistoryFile(m_settingsStore.filePath())))
    {
        setCredentialOperationError(tr("Delete encrypted AI history or keep the previous credential store before "
                                       "moving credentials to session storage."));
        return false;
    }
    if (m_aiConversationHistory)
    {
        m_aiConversationHistory->setVault(m_credentialVaults->active());
    }
    auto migrated = m_credentialVaults->migrate(*parsedTarget, false);
    if (!migrated)
    {
        setCredentialOperationError(credentialVaultErrorMessage(migrated.error()));
        return false;
    }

    config::ApplicationSettings updatedSettings = m_settings;
    updatedSettings.credentialStorage = credentialPreferenceForStorage(*parsedTarget);
    if (*parsedTarget == security::CredentialStorage::Session)
    {
        updatedSettings.aiConversationHistoryEnabled = false;
    }
    if (!persistApplicationSettings(updatedSettings))
    {
        m_credentialVaults->select(previousStorage);
        applyAiNetworkProxy();
        if (m_aiConversationHistory)
        {
            m_aiConversationHistory->setVault(m_credentialVaults->active());
        }
        setCredentialOperationError(tr(
            "Credentials were copied, but the storage preference could not be saved. The old store remains active."));
        emit credentialVaultChanged();
        return false;
    }
    emit hostProfilesChanged();
    applyAiNetworkProxy();
    if (m_aiConversationHistory)
    {
        m_aiConversationHistory->setVault(m_credentialVaults->active());
        if (m_settings.aiConversationHistoryEnabled)
        {
            m_aiConversationHistory->reload();
        }
    }
    if (removeSource)
    {
        auto cleaned = m_credentialVaults->removeAll(previousStorage);
        if (!cleaned)
        {
            setCredentialOperationError(tr("Migration succeeded, but credentials remain in the previous store."));
            emit credentialVaultChanged();
            return false;
        }
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::removeAllSavedCredentials()
{
    std::vector<security::CredentialKey> keys;
    const bool emptyUninitializedPortable = m_credentialVaults->storage() == security::CredentialStorage::Portable
                                            && !m_credentialVaults->portableInitialized();
    if (!emptyUninitializedPortable)
    {
        auto listedKeys = m_credentialVaults->active().listKeys();
        if (!listedKeys)
        {
            setCredentialOperationError(credentialVaultErrorMessage(listedKeys.error()));
            return false;
        }
        keys = std::move(*listedKeys);
    }
    std::vector<std::pair<security::CredentialKey, security::SensitiveByteArray>> backup;
    backup.reserve(keys.size());
    for (const security::CredentialKey &key : keys)
    {
        auto secret = m_credentialVaults->active().read(key);
        if (!secret)
        {
            setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
            return false;
        }
        backup.emplace_back(key, std::move(*secret));
    }
    auto removed = m_credentialVaults->removeAll();
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    std::vector<ssh::SshProfile> updated = m_profiles;
    for (ssh::SshProfile &profile : updated)
    {
        profile.credentialReference.reset();
    }
    if (!m_profileStore.save(updated))
    {
        for (const auto &[key, secret] : backup)
        {
            const std::string_view bytes = secret.view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "remove-all restore");
        }
        setCredentialOperationError(tr("Credentials were restored because host profiles could not be updated."));
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    return true;
}

bool AppController::clearCredentialStorage(const QString &target)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    if (*parsedTarget == m_credentialVaults->storage())
    {
        return removeAllSavedCredentials();
    }

    auto removed = m_credentialVaults->removeAll(*parsedTarget);
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::savePortForwardingRule(const QString &id, const QString &label, const QString &profileId,
                                           const QString &type, const QString &bindHost, const int bindPort,
                                           const QString &destinationHost, const int destinationPort,
                                           const bool autoStart)
{
    const auto parsedType = parseForwardingType(type.trimmed());
    const std::string resolvedProfileId = utf8String(profileId.trimmed());
    if (!parsedType || bindPort < 1 || bindPort > 65'535 || resolvedProfileId.empty()
        || std::ranges::find(m_profiles, resolvedProfileId, &ssh::SshProfile::id) == m_profiles.end())
    {
        m_portForwardingOperationError = tr("Choose a valid host profile, forwarding type, and bind port.");
        emit portForwardingRulesChanged();
        return false;
    }
    if (*parsedType != forwarding::PortForwardingType::Dynamic
        && (destinationPort < 1 || destinationPort > 65'535 || destinationHost.trimmed().isEmpty()))
    {
        m_portForwardingOperationError = tr("Enter a destination host and port for this forwarding rule.");
        emit portForwardingRulesChanged();
        return false;
    }

    forwarding::PortForwardingRule rule{
        .id = id.trimmed().isEmpty() ? utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                     : utf8String(id.trimmed()),
        .label = utf8String(label.trimmed()),
        .profileId = resolvedProfileId,
        .type = *parsedType,
        .bind = {.host = utf8String(bindHost.trimmed()), .port = static_cast<std::uint16_t>(bindPort)},
        .destination = *parsedType == forwarding::PortForwardingType::Dynamic
                           ? forwarding::PortForwardingEndpoint{}
                           : forwarding::PortForwardingEndpoint{.host = utf8String(destinationHost.trimmed()),
                                                                .port = static_cast<std::uint16_t>(destinationPort)},
        .autoStart = autoStart,
    };
    if (!forwarding::validPortForwardingRule(rule))
    {
        m_portForwardingOperationError = tr("Complete the forwarding rule with valid names and endpoints.");
        emit portForwardingRulesChanged();
        return false;
    }

    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    const auto existing = std::ranges::find(candidate, rule.id, &forwarding::PortForwardingRule::id);
    if (existing == candidate.end())
    {
        candidate.push_back(rule);
    }
    else
    {
        *existing = rule;
    }
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    stopPortForwardingRule(utf8QString(rule.id));
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::duplicatePortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto existing = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (existing == m_portForwardingRules.end())
    {
        m_portForwardingOperationError = tr("The forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }

    forwarding::PortForwardingRule duplicate = *existing;
    duplicate.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    duplicate.label = utf8String(tr("%1 copy").arg(utf8QString(existing->label)));
    duplicate.autoStart = false;
    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    candidate.push_back(std::move(duplicate));
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::copyPortForwardingBindEndpoint(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto rule = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == m_portForwardingRules.end())
    {
        return false;
    }
    QString host = utf8QString(rule->bind.host);
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
    {
        host = QStringLiteral("[%1]").arg(host);
    }
    QGuiApplication::clipboard()->setText(QStringLiteral("%1:%2").arg(host).arg(rule->bind.port));
    return true;
}

bool AppController::deletePortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    const auto rule = std::ranges::find(candidate, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == candidate.end())
    {
        return false;
    }
    candidate.erase(rule);
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    stopPortForwardingRule(id);
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::startPortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto rule = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == m_portForwardingRules.end())
    {
        m_portForwardingOperationError = tr("The forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }

    if (PortForwardingRuntime *existing = findPortForwardingRuntime(ruleId); existing != nullptr)
    {
        const auto state = existing->job->snapshot().state;
        if (state == forwarding::PortForwardingJobState::Starting
            || state == forwarding::PortForwardingJobState::Running)
        {
            return true;
        }
        stopPortForwardingRule(id);
    }
    if (m_portForwardingRuntimes.size() >= forwarding::maximumActivePortForwardingRuleCount)
    {
        m_portForwardingOperationError = tr("Stop another forwarding rule before starting this one.");
        emit portForwardingRulesChanged();
        return false;
    }

    const auto profile = std::ranges::find(m_profiles, rule->profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        m_portForwardingOperationError = tr("The host profile used by this forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }
    auto request = connectionRequestForProfile(*profile);
    if (!request)
    {
        m_portForwardingOperationError = m_credentialOperationError;
        emit portForwardingRulesChanged();
        return false;
    }

    PortForwardingRuntime *runtimePointer = nullptr;
    try
    {
        auto runtime = std::make_unique<PortForwardingRuntime>();
        runtime->ruleId = ruleId;
        runtime->job = std::make_unique<forwarding::PortForwardingJob>();
        runtimePointer = runtime.get();
        m_portForwardingRuntimes.push_back(std::move(runtime));
    }
    catch (const std::bad_alloc &)
    {
        m_portForwardingOperationError = tr("Unable to allocate the forwarding worker.");
        emit portForwardingRulesChanged();
        return false;
    }

    const QString qRuleId = utf8QString(ruleId);
    ssh::SshConnectionCallbacks callbacks{
        .confirmUnknownHostKey =
            [this, runtimePointer, qRuleId](const QString &endpoint, const QString &algorithm,
                                            const QString &fingerprint) {
                {
                    std::scoped_lock lock(runtimePointer->hostKeyMutex);
                    runtimePointer->hostKeyDecision.reset();
                    runtimePointer->awaitingHostKey = true;
                }
                try
                {
                    emit portForwardingHostKeyPromptRequested(qRuleId, endpoint, algorithm, fingerprint, false);
                }
                catch (...)
                {
                    resolvePortForwardingHostKey(*runtimePointer, ssh::UnknownHostKeyDecision::Reject);
                }
                std::unique_lock lock(runtimePointer->hostKeyMutex);
                runtimePointer->hostKeyAvailable.wait(lock, [runtimePointer] {
                    return runtimePointer->hostKeyDecision.has_value();
                });
                const ssh::UnknownHostKeyDecision decision = *runtimePointer->hostKeyDecision;
                runtimePointer->hostKeyDecision.reset();
                runtimePointer->awaitingHostKey = false;
                return decision;
            },
        .hostKeyChanged =
            [this, qRuleId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                try
                {
                    emit portForwardingHostKeyPromptRequested(qRuleId, endpoint, algorithm, fingerprint, true);
                }
                catch (...)
                {
                    return;
                }
            },
    };
    auto started = runtimePointer->job->start(
        *rule, std::move(*request), std::move(callbacks),
        [this, qRuleId](const forwarding::PortForwardingJobSnapshot &snapshot) noexcept {
            try
            {
                emit portForwardingSnapshotReady(
                    qRuleId, static_cast<int>(snapshot.state), static_cast<int>(snapshot.failure),
                    static_cast<qulonglong>(snapshot.activeClients), static_cast<qulonglong>(snapshot.bytesFromClients),
                    static_cast<qulonglong>(snapshot.bytesToClients),
                    static_cast<qulonglong>(snapshot.rejectedClients));
            }
            catch (...)
            {
                return;
            }
        });
    if (!started)
    {
        m_portForwardingRuntimes.erase(std::ranges::find(m_portForwardingRuntimes, runtimePointer,
                                                         [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                                             return candidate.get();
                                                         }));
        m_portForwardingOperationError = tr("Unable to start the forwarding worker.");
        emit portForwardingRulesChanged();
        return false;
    }
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

void AppController::stopPortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return candidate->ruleId;
                                           });
    if (runtime == m_portForwardingRuntimes.end())
    {
        return;
    }
    resolvePortForwardingHostKey(**runtime, ssh::UnknownHostKeyDecision::Reject);
    (*runtime)->job->stop();
    if (m_hostKeyForwardingRuleId == id)
    {
        clearHostKeyPrompt();
    }
    m_portForwardingRuntimes.erase(runtime);
    emit portForwardingRulesChanged();
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    const bool forSftp = m_hostKeyForSftp;
    const QString transferTaskId = m_hostKeyTransferTaskId;
    const QString forwardingRuleId = m_hostKeyForwardingRuleId;
    clearHostKeyPrompt();
    if (!forwardingRuleId.isEmpty())
    {
        if (PortForwardingRuntime *runtime = findPortForwardingRuntime(utf8String(forwardingRuleId));
            runtime != nullptr)
        {
            resolvePortForwardingHostKey(*runtime, remember ? ssh::UnknownHostKeyDecision::AcceptAndRemember
                                                            : ssh::UnknownHostKeyDecision::AcceptOnce);
        }
    }
    else if (!transferTaskId.isEmpty() && m_transferManager != nullptr)
    {
        m_transferManager->confirmHostKey(transferTaskId, remember);
    }
    else if (tab != nullptr && forSftp && tab->sftpSession)
    {
        tab->sftpSession->confirmHostKey(remember);
    }
    else if (tab != nullptr && tab->ssh)
    {
        tab->ssh->confirmHostKey(remember);
    }
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    const bool forSftp = m_hostKeyForSftp;
    const QString transferTaskId = m_hostKeyTransferTaskId;
    const QString forwardingRuleId = m_hostKeyForwardingRuleId;
    clearHostKeyPrompt();
    if (!forwardingRuleId.isEmpty())
    {
        if (PortForwardingRuntime *runtime = findPortForwardingRuntime(utf8String(forwardingRuleId));
            runtime != nullptr)
        {
            resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
        }
    }
    else if (!transferTaskId.isEmpty() && m_transferManager != nullptr)
    {
        m_transferManager->rejectHostKey(transferTaskId);
    }
    else if (tab != nullptr && forSftp && tab->sftpSession)
    {
        tab->sftpSession->rejectHostKey();
    }
    else if (tab != nullptr && tab->ssh)
    {
        tab->ssh->rejectHostKey();
    }
}

void AppController::initializeSessionLog(TerminalTab &tab)
{
    tab.sessionLog = std::make_shared<logging::SessionLogWriter>();
    const QString tabId = tab.id;
    QObject::connect(tab.sessionLog.get(), &logging::SessionLogWriter::stateChanged, this, [this, tabId] {
        TerminalTab *updated = findTab(tabId);
        if (updated == nullptr || updated->sessionLog == nullptr)
        {
            return;
        }
        if (updated->sessionLog->state() == logging::SessionLogState::Failed)
        {
            updated->status = tr("Session log failed: %1").arg(updated->sessionLog->errorString());
            if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
            {
                terminal->setStatusText(updated->status);
            }
        }
        emit terminalTabsChanged();
    });
}

void AppController::initializeTerminalOutputSink(TerminalTab &tab)
{
    initializeAiRuntime(tab);
    const QString tabId = tab.id;
    const std::string nonce = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tab.semanticObserver = std::make_shared<terminal::SemanticTerminalObserver>(
        terminal::CommandBlockSessionContext{
            .sessionId = utf8String(tab.id),
            .host = utf8String(tab.address),
            .shell = tab.kind == TerminalTabKind::Local ? "pwsh" : std::string{},
            .sessionGeneration = tab.reconnectGeneration,
        },
        nonce);
    tab.aiFrameTracker = std::make_shared<ai::AiTerminalFrameTracker>();
    const auto semanticObserver = tab.semanticObserver;
    const auto frameTracker = tab.aiFrameTracker;
    if (tab.local)
    {
        tab.local->setShellIntegrationNonce(nonce);
    }
    tab.outputSink = std::make_shared<TerminalOutputFanout>(
        tab.sessionLog, [this, tabId, semanticObserver, frameTracker](const std::span<const std::byte> bytes) {
            if (bytes.empty())
            {
                return;
            }
            semanticObserver->append(bytes);
            frameTracker->observeOutput(bytes);
            const QByteArray copy(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
            emit scriptOutputObserved(tabId, copy);
        });
    if (tab.local)
    {
        tab.local->setOutputSink(tab.outputSink);
    }
    else if (tab.ssh)
    {
        tab.ssh->setOutputSink(tab.outputSink);
    }
}

void AppController::initializeAiRuntime(TerminalTab &tab)
{
    if (tab.aiConversationId.isEmpty())
    {
        tab.aiConversationId = tab.id + QLatin1Char(':') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!tab.aiConversation)
    {
        tab.aiConversation = std::make_unique<ai::AiConversationModel>();
    }
    if (!tab.aiTurnRunner)
    {
        tab.aiTurnRunner = std::make_unique<ai::AiTurnRunner>(m_aiProviderClient);
    }
}

void AppController::initializeAiConversationHistory()
{
    m_aiConversationHistory = std::make_unique<ai::AiConversationHistoryModel>(
        siblingAiConversationHistoryFile(m_settingsStore.filePath()), m_credentialVaults->active(), this);
    if (m_settings.aiConversationHistoryEnabled)
    {
        if (m_credentialVaults->active().persistent())
        {
            m_aiConversationHistory->reload();
        }
        else
        {
            m_settings.aiConversationHistoryEnabled = false;
            const auto saved = m_settingsStore.save(m_settings);
            if (!saved)
            {
                qCWarning(appControllerLog) << "Could not persist disabled AI conversation history setting";
            }
        }
    }
}

void AppController::persistAiConversation(const TerminalTab &tab)
{
    if (!m_settings.aiConversationHistoryEnabled || !m_aiConversationHistory || !tab.aiConversation)
    {
        return;
    }
    const auto transcript = tab.aiConversation->transcript();
    if (transcript.empty())
    {
        return;
    }
    ai::AiStoredConversation stored{.id = tab.aiConversationId, .updatedAtUtc = QDateTime::currentDateTimeUtc()};
    stored.messages.reserve(transcript.size());
    for (const auto &entry : transcript)
    {
        QString role;
        switch (entry.role)
        {
            case ai::AiConversationTranscriptRole::user:
                role = QStringLiteral("user");
                break;
            case ai::AiConversationTranscriptRole::assistant:
                role = QStringLiteral("assistant");
                break;
            case ai::AiConversationTranscriptRole::evidence:
                role = QStringLiteral("evidence");
                break;
        }
        const QString text = utf8QString(entry.content);
        if (stored.title.isEmpty() && entry.role == ai::AiConversationTranscriptRole::user)
        {
            stored.title = text.simplified().left(80);
        }
        stored.messages.push_back({.role = std::move(role),
                                   .text = text,
                                   .reasoning = utf8QString(entry.reasoning),
                                   .toolActivities = entry.toolActivities,
                                   .sources = entry.sources,
                                   .providerReplayJson = entry.providerReplayJson,
                                   .usage = entry.usage,
                                   .metrics = entry.metrics,
                                   .estimatedCostUsd = entry.estimatedCostUsd,
                                   .costCatalogDate = utf8QString(entry.costCatalogDate),
                                   .longContextRates = entry.longContextRates,
                                   .truncated = entry.truncated,
                                   .contextAttachments = entry.contextAttachments});
    }
    if (stored.title.isEmpty())
    {
        stored.title = tr("AI conversation");
    }
    if (!stored.messages.empty())
    {
        m_aiConversationHistory->persist(std::move(stored));
    }
}

void AppController::observeScriptOutput(const QString &tabId, const QByteArray &bytes)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || bytes.isEmpty() || !tab->scriptExecution.active())
    {
        return;
    }
    const workbench::ScriptExecutionSnapshot before = tab->scriptExecution.snapshot();
    const auto view = std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    const std::vector<std::string> commands =
        tab->scriptExecution.observeOutput(std::as_bytes(view), scriptExecutionNow());
    dispatchScriptCommands(*tab, commands);
    if (tab->scriptExecution.snapshot() != before)
    {
        emit terminalTabsChanged();
    }
}

void AppController::dispatchScriptCommands(TerminalTab &tab, const std::vector<std::string> &commands)
{
    for (const std::string &command : commands)
    {
        const QString normalized = normalizedQuickCommandText(utf8QString(command));
        if (!validTerminalCommand(normalized))
        {
            static_cast<void>(tab.scriptExecution.cancel());
            return;
        }
        QByteArray bytes = normalized.toUtf8();
        bytes.replace('\n', '\r');
        if (bytes.isEmpty() || bytes.back() != '\r')
        {
            bytes.append('\r');
        }
        appendCapturedHistory(tab, normalized);
        tab.inputHistoryBuffer.clear();
        tab.inputHistoryBufferReliable = true;
        dispatchInput(tab, bytes);
    }
}

void AppController::initializeScriptExecutionTimer()
{
    m_scriptExecutionTimer.setInterval(100);
    m_scriptExecutionTimer.setTimerType(Qt::CoarseTimer);
    QObject::connect(&m_scriptExecutionTimer, &QTimer::timeout, this, [this] {
        bool changed = false;
        const auto now = scriptExecutionNow();
        for (const auto &tab : m_tabs)
        {
            changed = tab->scriptExecution.tick(now) || changed;
        }
        if (changed)
        {
            emit terminalTabsChanged();
        }
    });
    m_scriptExecutionTimer.start();
}

void AppController::connectLocalTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (updated->aiFrameTracker)
                         {
                             updated->aiFrameTracker->observeFrame(terminalFrameInput(snapshot));
                         }
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         const TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr)
                         {
                             if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                             {
                                 terminal->setClipboardText(text);
                             }
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::selectedTextReady, this,
                     [this, tabId](const QString &text) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             acceptAiSelectedText(*updated, text);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::runningChanged, this,
                     [this, tabId](const bool running) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->running = running;
                             if (running && updated->connectedUtcMs == 0)
                             {
                                 updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                             }
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_focusedTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
}

void AppController::connectSshTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (updated->aiFrameTracker)
                         {
                             updated->aiFrameTracker->observeFrame(terminalFrameInput(snapshot));
                         }
                         const QString workingDirectory =
                             snapshot ? normalizedTerminalWorkingDirectory(snapshot->workingDirectory) : QString{};
                         const bool workingDirectoryChanged = updated->terminalWorkingDirectory != workingDirectory;
                         updated->terminalWorkingDirectory = workingDirectory;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setSnapshot(snapshot);
                         }
                         if (workingDirectoryChanged && updated->followTerminalDirectory
                             && updated->sftpSession != nullptr && updated->sftpState == QStringLiteral("ready")
                             && !workingDirectory.isEmpty() && workingDirectory != updated->sftpPath)
                         {
                             requestSftpDirectory(*updated, workingDirectory);
                         }
                         else if (workingDirectoryChanged && m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         const TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr)
                         {
                             if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                             {
                                 terminal->setClipboardText(text);
                             }
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::selectedTextReady, this,
                     [this, tabId](const QString &text) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             acceptAiSelectedText(*updated, text);
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::runningChanged, this, [this, tabId](const bool running) {
        if (TerminalTab *updated = findTab(tabId))
        {
            updated->running = running;
            if (running && updated->connectedUtcMs == 0)
            {
                updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
            }
            emit terminalTabsChanged();
            updateTelemetryVisibility();
        }
    });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::phaseChanged, this,
                     [this, tabId](const ssh::SshConnectionPhase phase) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshPhase = phase;
                             if (phase == ssh::SshConnectionPhase::Connected)
                             {
                                 updated->reconnectPending = false;
                                 updated->sshFailure.reset();
                                 updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                                 recordRecentConnection(*updated);
                             }
                             else if (phase == ssh::SshConnectionPhase::Failed && updated->sshFailure)
                             {
                                 scheduleSshReconnect(*updated, *updated->sshFailure);
                             }
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::failureOccurred, this,
                     [this, tabId](const ssh::SshFailureKind failure) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshFailure = failure;
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_focusedTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
    QObject::connect(
        tab.ssh.get(), &ssh::SshTerminalSession::shellHistoryReady, this,
        [this, tabId](const quint64 requestId, const QString &shell, const QByteArray &contents, const QString &error) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || updated->historyRequestId != requestId)
            {
                return;
            }
            if (!error.isEmpty())
            {
                updated->history.clear();
                updated->historyState = QStringLiteral("error");
                updated->historyError = error;
                if (m_focusedTabId == tabId)
                {
                    emit terminalHistoryChanged();
                }
                return;
            }
            constexpr qsizetype maximumHistoryBytes = qsizetype{2} * 1024 * 1024;
            if (contents.size() > maximumHistoryBytes)
            {
                updated->history.clear();
                updated->historyState = QStringLiteral("error");
                updated->historyError = tr("Remote shell history exceeded the safety limit.");
                if (m_focusedTabId == tabId)
                {
                    emit terminalHistoryChanged();
                }
                return;
            }

            const QPointer<AppController> self(this);
            QThreadPool::globalInstance()->start([self, tabId, requestId, shell, contents] {
                std::vector<workbench::ShellHistoryEntry> parsed;
                QString parseError;
                try
                {
                    const std::string_view source(contents.constData(), static_cast<std::size_t>(contents.size()));
                    if (shell == QStringLiteral("bash"))
                    {
                        parsed = workbench::parseBashHistory(source);
                    }
                    else if (shell == QStringLiteral("zsh"))
                    {
                        parsed = workbench::parseZshHistory(source);
                    }
                    else if (shell == QStringLiteral("fish"))
                    {
                        parsed = workbench::parseFishHistory(source);
                    }
                    else
                    {
                        parseError = AppController::tr("This remote shell is not supported yet.");
                    }
                }
                catch (const std::bad_alloc &)
                {
                    parseError = AppController::tr("Remote shell history could not be parsed.");
                }
                if (self)
                {
                    emit self->terminalHistoryTaskCompleted(tabId, requestId, std::move(parsed), parseError);
                }
            });
        });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::remoteTelemetryReady, this,
                     [this, tabId](const telemetry::Sample &sample) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->telemetrySample = sample;
                         updated->telemetryHistory.push_back(sample);
                         while (updated->telemetryHistory.size() > telemetry::maximumHistorySamples)
                         {
                             updated->telemetryHistory.pop_front();
                         }
                         if (m_focusedTabId == tabId)
                         {
                             emit remoteTelemetryChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::remoteTelemetryStateChanged, this,
                     [this, tabId](const QString &state) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->telemetryState == state)
                         {
                             return;
                         }
                         updated->telemetryState = state;
                         if (m_focusedTabId == tabId)
                         {
                             emit remoteTelemetryChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = false;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, false);
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = false;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(tr("SSH host key changed; connection blocked"));
                         }
                     });
}

void AppController::updateTelemetryVisibility()
{
    for (const auto &tab : m_tabs)
    {
        if (tab->ssh)
        {
            tab->ssh->setRemoteTelemetryVisible(m_terminalTelemetryVisible && tab->id == m_focusedTabId
                                                && tab->sshPhase == ssh::SshConnectionPhase::Connected);
        }
    }
}

void AppController::connectSftpTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::runningChanged, this,
                     [this, tabId](const bool running) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpSession == nullptr)
                         {
                             return;
                         }
                         if (!running && updated->sftpState != QStringLiteral("error"))
                         {
                             updated->sftpState = QStringLiteral("idle");
                             emit sftpChanged();
                         }
                     });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::homeDirectoryReady, this, [this, tabId](const QString &homePath) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || updated->sftpSession == nullptr)
            {
                return;
            }
            updated->sftpHomePath = homePath;
            if (!updated->pendingDropLocalFiles.isEmpty())
            {
                const QStringList pending = std::exchange(updated->pendingDropLocalFiles, {});
                QString destination = updated->terminalWorkingDirectory;
                if (!sftp::normalizeRemotePath(utf8String(destination)))
                {
                    destination = homePath;
                }
                if (!enqueueSftpUploadBatchForTab(*updated, pending, destination))
                {
                    updated->status = tr("The dropped files could not be queued for upload.");
                    emit terminalTabsChanged();
                }
            }
            const QString requestedPath =
                !updated->sftpHasListing && updated->sftpPath == QStringLiteral("/") ? homePath : updated->sftpPath;
            requestSftpDirectory(*updated, requestedPath);
        });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::directoryReady, this,
                     [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                                   const sftp::DirectoryListingPtr &entries) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpModel == nullptr || requestId != updated->sftpRequestId
                             || generation != updated->sftpGeneration)
                         {
                             return;
                         }
                         updated->sftpPath = remotePath;
                         updated->sftpRequestedPath = remotePath;
                         updated->sftpState = QStringLiteral("ready");
                         updated->sftpError.clear();
                         updated->sftpHasListing = true;
                         updated->sftpModel->setEntries(entries, remotePath);
                         persistWorkspaceState(*updated, true);
                         if (m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::treeDirectoryReady, this,
        [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                      const sftp::DirectoryListingPtr &entries) {
            TerminalTab *updated = findTab(tabId);
            if ((requestId & aiSftpListRequestFlag) != 0)
            {
                if (updated == nullptr || !updated->pendingAiSftpList.has_value()
                    || updated->pendingAiSftpList->requestId != requestId || entries == nullptr
                    || updated->pendingAiSftpList->request.target.sessionGeneration != generation
                    || utf8QString(updated->pendingAiSftpList->request.remotePath) != remotePath
                    || !aiTurnActive(*updated))
                {
                    return;
                }
                const auto pending = std::move(*updated->pendingAiSftpList);
                updated->pendingAiSftpList.reset();
                const auto output =
                    updated->reconnectGeneration == generation
                        ? ai::AiSftpListTool::result(pending.request, *entries)
                        : ai::AiSftpListTool::failure(
                              "scope_changed", "The requested terminal session or generation is no longer active.");
                const QString resultCode = aiActivityResultCode(output);
                recordAiActivity(*updated, pending.call,
                                 resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                    : QStringLiteral("failed"),
                                 resultCode, false);
                static_cast<void>(completePendingAiTool(
                    *updated,
                    ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
                return;
            }
            if (updated == nullptr || updated->sftpModel == nullptr || generation != updated->sftpGeneration)
            {
                return;
            }
            updated->sftpModel->applyTreeEntries(remotePath, entries);
        });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::treeDirectoryFailed, this,
        [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                      const ssh::SshTransportErrorKind error) {
            TerminalTab *updated = findTab(tabId);
            if ((requestId & aiSftpListRequestFlag) != 0)
            {
                if (updated == nullptr || !updated->pendingAiSftpList.has_value()
                    || updated->pendingAiSftpList->requestId != requestId
                    || updated->pendingAiSftpList->request.target.sessionGeneration != generation
                    || utf8QString(updated->pendingAiSftpList->request.remotePath) != remotePath
                    || !aiTurnActive(*updated))
                {
                    return;
                }
                const auto pending = std::move(*updated->pendingAiSftpList);
                updated->pendingAiSftpList.reset();
                const auto output =
                    updated->reconnectGeneration == generation
                        ? ai::AiSftpListTool::failure(error)
                        : ai::AiSftpListTool::failure(
                              "scope_changed", "The requested terminal session or generation is no longer active.");
                const QString resultCode = aiActivityResultCode(output);
                recordAiActivity(*updated, pending.call, QStringLiteral("failed"), resultCode, false);
                static_cast<void>(completePendingAiTool(
                    *updated,
                    ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
                return;
            }
            if (updated == nullptr || updated->sftpModel == nullptr || generation != updated->sftpGeneration)
            {
                return;
            }
            updated->sftpModel->applyTreeError(remotePath, tr("This folder could not be expanded."));
        });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::fileReadReady, this,
        [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                      const sftp::FileReadBytesPtr &bytes, const bool truncated) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || !updated->pendingAiSftpRead.has_value() || bytes == nullptr
                || updated->pendingAiSftpRead->requestId != requestId
                || updated->pendingAiSftpRead->request.target.sessionGeneration != generation
                || utf8QString(updated->pendingAiSftpRead->request.remotePath) != remotePath || !aiTurnActive(*updated))
            {
                return;
            }
            const auto pending = std::move(*updated->pendingAiSftpRead);
            updated->pendingAiSftpRead.reset();
            if (updated->reconnectGeneration != generation)
            {
                const auto output = ai::AiSftpReadTool::failure(
                    "scope_changed", "The requested terminal session or generation is no longer active.");
                recordAiActivity(*updated, pending.call, QStringLiteral("failed"), QStringLiteral("scope_changed"),
                                 false);
                static_cast<void>(completePendingAiTool(
                    *updated,
                    ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
                return;
            }
            const auto output = ai::AiSftpReadTool::result(pending.request, *bytes, truncated);
            const QString resultCode = aiActivityResultCode(output);
            recordAiActivity(*updated, pending.call,
                             resultCode == QStringLiteral("ok") ? QStringLiteral("succeeded")
                                                                : QStringLiteral("failed"),
                             resultCode, false);
            static_cast<void>(completePendingAiTool(
                *updated,
                ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
        });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::fileReadFailed, this,
        [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                      const ssh::SshTransportErrorKind error) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || !updated->pendingAiSftpRead.has_value()
                || updated->pendingAiSftpRead->requestId != requestId
                || updated->pendingAiSftpRead->request.target.sessionGeneration != generation
                || utf8QString(updated->pendingAiSftpRead->request.remotePath) != remotePath || !aiTurnActive(*updated))
            {
                return;
            }
            const auto pending = std::move(*updated->pendingAiSftpRead);
            updated->pendingAiSftpRead.reset();
            if (updated->reconnectGeneration != generation)
            {
                const auto output = ai::AiSftpReadTool::failure(
                    "scope_changed", "The requested terminal session or generation is no longer active.");
                recordAiActivity(*updated, pending.call, QStringLiteral("failed"), QStringLiteral("scope_changed"),
                                 false);
                static_cast<void>(completePendingAiTool(
                    *updated,
                    ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
                return;
            }
            const auto output = ai::AiSftpReadTool::failure(error);
            const QString resultCode = aiActivityResultCode(output);
            recordAiActivity(*updated, pending.call, QStringLiteral("failed"), resultCode, false);
            static_cast<void>(completePendingAiTool(
                *updated,
                ai::AiToolOutput{.callId = pending.call.id, .name = pending.call.name, .outputJson = output}));
        });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::operationSucceeded, this,
                     [this, tabId](const quint64, const sftp::SftpOperationKind) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr && updated->sftpSession != nullptr)
                         {
                             requestSftpDirectory(*updated, updated->sftpPath);
                         }
                     });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::operationFailed, this,
        [this, tabId](const quint64, const sftp::SftpOperationKind operation, const ssh::SshTransportErrorKind) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr)
            {
                return;
            }
            if (operation == sftp::SftpOperationKind::ListDirectory)
            {
                updated->sftpState = updated->sftpHasListing ? QStringLiteral("ready") : QStringLiteral("error");
                updated->sftpError = tr("The remote directory could not be loaded.");
            }
            else
            {
                updated->sftpError = tr("The remote file operation failed.");
            }
            if (m_focusedTabId == tabId)
            {
                emit sftpChanged();
            }
        });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::connectionFailed, this,
                     [this, tabId](const ssh::SshFailureKind) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->sftpState = QStringLiteral("error");
                         updated->sftpError = tr("The SFTP connection failed.");
                         if (m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = true;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, false);
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::hostKeyChanged, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = true;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, true);
                     });
}

void AppController::recordRecentConnection(TerminalTab &tab)
{
    if (tab.recentConnectionRecorded || tab.sourceProfileId.isEmpty())
    {
        return;
    }
    tab.recentConnectionRecorded = true;
    const auto profile = std::ranges::find(m_profiles, utf8String(tab.sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return;
    }

    const std::optional<std::int64_t> previous = profile->lastConnectedUtcMs;
    profile->lastConnectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (!m_profileStore.save(m_profiles))
    {
        profile->lastConnectedUtcMs = previous;
        qCWarning(appControllerLog) << "Unable to persist recent SSH connection metadata";
        return;
    }
    emit hostProfilesChanged();
}

void AppController::connectTerminalSignals(ui::TerminalItem &terminal, const QString &paneId)
{
    QObject::connect(&terminal, &ui::TerminalItem::inputGenerated, this, [this, paneId](const QByteArray &bytes) {
        if (activateTerminalPane(paneId))
        {
            queueInput(bytes);
        }
    });
    QObject::connect(
        &terminal, &ui::TerminalItem::keyEventGenerated, this, [this, paneId](const terminal::TerminalKeyEvent &event) {
            if (!activateTerminalPane(paneId))
            {
                return;
            }
            TerminalTab *tab = findTabForPane(paneId);
            if (tab == nullptr)
            {
                return;
            }

            QByteArray observed;
            if (event.action != terminal::TerminalKeyAction::release)
            {
                if (!event.text.empty())
                {
                    observed = QByteArray(event.text.data(), static_cast<qsizetype>(event.text.size()));
                }
                else if (event.key == terminal::TerminalKey::enter || event.key == terminal::TerminalKey::numpadEnter)
                {
                    observed = QByteArrayLiteral("\r");
                }
                else if (event.key == terminal::TerminalKey::backspace)
                {
                    observed = QByteArray(1, '\x7f');
                }
                else if ((event.modifiers & terminal::terminalModifierControl) != 0
                         && (event.key == terminal::TerminalKey::keyU || event.key == terminal::TerminalKey::keyC))
                {
                    observed = QByteArray(1, event.key == terminal::TerminalKey::keyU ? static_cast<char>(0x15)
                                                                                      : static_cast<char>(0x03));
                }
                else if (event.key != terminal::TerminalKey::shiftLeft && event.key != terminal::TerminalKey::shiftRight
                         && event.key != terminal::TerminalKey::controlLeft
                         && event.key != terminal::TerminalKey::controlRight
                         && event.key != terminal::TerminalKey::altLeft && event.key != terminal::TerminalKey::altRight
                         && event.key != terminal::TerminalKey::metaLeft
                         && event.key != terminal::TerminalKey::metaRight)
                {
                    // A non-text VT key can move or edit the remote line. Preserve the
                    // old observer's conservative behavior without feeding its encoded
                    // Kitty/modifyOtherKeys escape sequence into command history.
                    observed = QByteArray(1, '\x1b');
                }
            }
            if (!observed.isEmpty())
            {
                observeUserInput(*tab, observed);
                observeTerminalInput(*tab, observed);
            }
            if (tab->ssh)
            {
                tab->ssh->queueKeyEvent(event);
            }
            else if (tab->local)
            {
                tab->local->queueKeyEvent(event);
            }
        });
    QObject::connect(&terminal, &ui::TerminalItem::mouseEventGenerated, this,
                     [this, paneId](const terminal::TerminalMouseEvent &event) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->queueMouseEvent(event);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->queueMouseEvent(event);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::focusEventGenerated, this, [this, paneId](const bool focused) {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->queueFocusEvent(focused);
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->queueFocusEvent(focused);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::pasteRequested, this, [this, paneId](const QByteArray &bytes) {
        if (activateTerminalPane(paneId))
        {
            queuePaste(bytes);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::localFilesDropped, this, [this, paneId](const QStringList &paths) {
        if (paths.isEmpty() || !activateTerminalPane(paneId))
        {
            return;
        }
        TerminalTab *tab = findTabForPane(paneId);
        if (tab == nullptr)
        {
            return;
        }
        if (tab->kind == TerminalTabKind::Local)
        {
            std::vector<std::string> absolutePaths;
            absolutePaths.reserve(static_cast<std::size_t>(paths.size()));
            for (const QString &path : paths)
            {
                const QFileInfo source(path);
                if (!source.exists())
                {
                    return;
                }
                absolutePaths.push_back(utf8String(source.absoluteFilePath()));
            }
            const std::string quoted = terminal::quoteShellPaths(absolutePaths, terminal::ShellDialect::PowerShell);
            queuePaste(QByteArray(quoted.data(), static_cast<qsizetype>(quoted.size())));
            return;
        }

        QString destination = tab->terminalWorkingDirectory;
        if (!sftp::normalizeRemotePath(utf8String(destination)))
        {
            destination.clear();
        }
        if (destination.isEmpty() && tab->sftpHasListing)
        {
            destination = tab->sftpPath;
        }
        if (destination.isEmpty())
        {
            destination = tab->sftpHomePath;
        }
        if (!destination.isEmpty())
        {
            if (!enqueueSftpUploadBatchForTab(*tab, paths, destination))
            {
                tab->status = tr("The dropped files could not be queued for upload.");
                emit terminalTabsChanged();
            }
            return;
        }

        tab->pendingDropLocalFiles = paths;
        tab->status = tr("Preparing the remote home directory for upload...");
        if (tab->sftpSession == nullptr && !startSftpSession(*tab))
        {
            tab->pendingDropLocalFiles.clear();
            tab->status = tr("Open SFTP before dropping files on this terminal.");
        }
        emit terminalTabsChanged();
    });
    QObject::connect(&terminal, &ui::TerminalItem::sizeRequested, this,
                     [this, paneId](const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                    const quint32 cellHeightPixels) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::scrollRequested, this, [this, paneId](const int rows) {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->requestScroll(rows);
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->requestScroll(rows);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::selectionRequested, this,
                     [this, paneId](const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                    const quint16 endRow, const bool rectangular) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::selectionGestureRequested, this,
                     [this, paneId](const terminal::TerminalSelectionGesture &gesture) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestSelectionGesture(gesture);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestSelectionGesture(gesture);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::copyModeActionRequested, this,
                     [this, paneId](const terminal::TerminalCopyModeAction &action) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestCopyModeAction(action);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestCopyModeAction(action);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::selectAllRequested, this, [this, paneId] {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->selectAll();
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->selectAll();
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::clearSelectionRequested, this, [this, paneId] {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->clearSelection();
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->clearSelection();
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::copyRequested, this, [this, paneId] {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->copySelection();
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->copySelection();
        }
    });
}

void AppController::queueInput(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || bytes.isEmpty())
    {
        return;
    }
    observeUserInput(*tab, bytes);
    observeTerminalInput(*tab, bytes);
    dispatchInput(*tab, bytes);
}

void AppController::queuePaste(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || bytes.isEmpty())
    {
        return;
    }
    observeUserInput(*tab, bytes);
    observeTerminalInput(*tab, bytes);
    dispatchPaste(*tab, bytes);
}

void AppController::observeUserInput(TerminalTab &tab, const QByteArray &bytes)
{
    const ai::AiSessionTarget target{.sessionId = utf8String(tab.id), .sessionGeneration = tab.reconnectGeneration};
    const std::string conversationId = utf8String(tab.aiConversationId);
    // Live user input preempts an Agent that holds the session write lease:
    // a completed line hands control back, any other keystroke takes control.
    // Without this handoff the Agent would keep writing into the middle of the
    // user's in-progress command line.
    const bool lineCompleted = std::ranges::any_of(bytes, [](const char value) {
        return value == '\r' || value == '\n';
    });
    if (lineCompleted)
    {
        static_cast<void>(m_aiActionToolDispatcher.resumeAgent(target, conversationId));
    }
    else if (m_aiActionToolDispatcher.agentHasControl(target, conversationId))
    {
        static_cast<void>(m_aiActionToolDispatcher.handoffToUser(target, conversationId));
    }
}

bool AppController::aiUserHasPendingLine(const TerminalTab &tab) const
{
    return tab.inputHistoryBufferReliable && !tab.inputHistoryBuffer.isEmpty();
}

void AppController::dispatchInput(TerminalTab &tab, const QByteArray &bytes)
{
    if (tab.ssh)
    {
        tab.ssh->queueInput(bytes);
    }
    else if (tab.local)
    {
        tab.local->queueInput(bytes);
    }
}

void AppController::dispatchPaste(TerminalTab &tab, const QByteArray &bytes)
{
    if (tab.ssh)
    {
        tab.ssh->queuePaste(bytes);
    }
    else if (tab.local)
    {
        tab.local->queuePaste(bytes);
    }
}

void AppController::observeTerminalInput(TerminalTab &tab, const QByteArray &bytes)
{
    for (const char character : bytes)
    {
        const auto value = static_cast<unsigned char>(character);
        if (character == '\r' || character == '\n')
        {
            if (tab.inputHistoryBufferReliable)
            {
                const QString command = QString::fromUtf8(tab.inputHistoryBuffer);
                appendCapturedHistory(tab, command);
                if (tab.semanticObserver && !command.isEmpty())
                {
                    tab.semanticObserver->observeFallbackCommand(utf8String(command));
                }
            }
            tab.inputHistoryBuffer.clear();
            tab.inputHistoryBufferReliable = true;
            continue;
        }
        if (value == 0x08U || value == 0x7FU)
        {
            if (tab.inputHistoryBufferReliable)
            {
                removeLastUtf8CodePoint(tab.inputHistoryBuffer);
            }
            continue;
        }
        if (value == 0x15U || value == 0x03U)
        {
            tab.inputHistoryBuffer.clear();
            tab.inputHistoryBufferReliable = true;
            continue;
        }
        if (value < 0x20U)
        {
            tab.inputHistoryBufferReliable = false;
            continue;
        }
        if (tab.inputHistoryBufferReliable)
        {
            tab.inputHistoryBuffer.append(character);
            if (tab.inputHistoryBuffer.size() > maximumPendingHistoryBytes)
            {
                tab.inputHistoryBuffer.clear();
                tab.inputHistoryBufferReliable = false;
            }
        }
    }
}

void AppController::appendCapturedHistory(TerminalTab &tab, const QString &command)
{
    const QString normalized = normalizedQuickCommandText(command).trimmed();
    if (!validTerminalCommand(normalized))
    {
        return;
    }

    const workbench::ShellKind shell =
        tab.kind == TerminalTabKind::Local
            ? workbench::ShellKind::powershell
            : (!tab.history.empty() ? tab.history.front().shell : workbench::ShellKind::unknown);
    const std::int64_t timestamp = QDateTime::currentSecsSinceEpoch();
    if (!tab.capturedHistory.empty() && utf8QString(tab.capturedHistory.front().command) == normalized)
    {
        tab.capturedHistory.front().timestampUtcSeconds = timestamp;
    }
    else
    {
        tab.capturedHistory.insert(tab.capturedHistory.begin(),
                                   workbench::ShellHistoryEntry{.command = utf8String(normalized),
                                                                .shell = shell,
                                                                .timestampUtcSeconds = timestamp});
        if (tab.capturedHistory.size() > maximumHistoryEntries)
        {
            tab.capturedHistory.resize(maximumHistoryEntries);
        }
    }
    emit terminalHistoryChanged();
}

void AppController::requestScroll(const int rows)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestScroll(rows);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestScroll(rows);
    }
}

void AppController::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                     const quint16 endRow, const bool rectangular)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
}

void AppController::clearSelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->clearSelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->clearSelection();
    }
}

void AppController::copySelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->copySelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->copySelection();
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

AppController::TerminalTab *AppController::activeTab()
{
    return findTab(m_focusedTabId);
}

const AppController::TerminalTab *AppController::activeTab() const
{
    return findTab(m_focusedTabId);
}

AppController::TerminalTab *AppController::findTab(const QString &id)
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTab(const QString &id) const
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

AppController::TerminalTab *AppController::findTabForPane(const QString &paneId)
{
    const auto tab = std::ranges::find(m_tabs, paneId, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->paneId;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTabForPane(const QString &paneId) const
{
    const auto tab = std::ranges::find(m_tabs, paneId, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->paneId;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

workbench::TerminalWorkspaceLayout *AppController::findTerminalWorkspace(const QString &id)
{
    const auto workspace =
        std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(id), &workbench::TerminalWorkspaceLayout::id);
    return workspace == m_workspaceState.terminalWorkspaces.end() ? nullptr : &*workspace;
}

const workbench::TerminalWorkspaceLayout *AppController::findTerminalWorkspace(const QString &id) const
{
    const auto workspace =
        std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(id), &workbench::TerminalWorkspaceLayout::id);
    return workspace == m_workspaceState.terminalWorkspaces.end() ? nullptr : &*workspace;
}

QString AppController::firstTabIdForWorkspace(const workbench::TerminalWorkspaceLayout &workspace) const
{
    const std::vector<std::string> panes = workbench::terminalPaneOrder(workspace);
    if (panes.empty())
    {
        return {};
    }
    const TerminalTab *tab = findTabForPane(utf8QString(panes.front()));
    return tab == nullptr ? QString{} : tab->id;
}

bool AppController::persistTerminalWorkspaces()
{
    return saveWorkspaceStateCandidate(m_workspaceState);
}

bool AppController::saveWorkspaceStateCandidate(const workbench::WorkspaceState &candidate)
{
    workbench::WorkspaceState persistable = candidate;
    std::erase_if(persistable.terminalWorkspaces, [](const workbench::TerminalWorkspaceLayout &workspace) {
        return std::ranges::any_of(workspace.restoreIntents, [](const workbench::TerminalRestoreIntent &intent) {
            return intent.kind == workbench::TerminalRestoreKind::Transient;
        });
    });
    const auto active = std::ranges::find(persistable.terminalWorkspaces, persistable.activeTerminalWorkspaceId,
                                          &workbench::TerminalWorkspaceLayout::id);
    if (active == persistable.terminalWorkspaces.end())
    {
        persistable.activeTerminalWorkspaceId =
            persistable.terminalWorkspaces.empty() ? std::string{} : persistable.terminalWorkspaces.front().id;
    }
    if (!workbench::validWorkspaceState(persistable))
    {
        qCWarning(appControllerLog) << "Terminal workspace candidate failed domain validation"
                                    << "profiles=" << persistable.profiles.size()
                                    << "workspaces=" << persistable.terminalWorkspaces.size()
                                    << "activeWorkspaceEmpty=" << persistable.activeTerminalWorkspaceId.empty();
        for (const workbench::TerminalWorkspaceLayout &workspace : persistable.terminalWorkspaces)
        {
            qCWarning(appControllerLog) << "workspaceValid=" << workbench::validTerminalWorkspaceLayout(workspace)
                                        << "nodes=" << workspace.nodes.size()
                                        << "intents=" << workspace.restoreIntents.size();
        }
    }
    const auto saved = m_workspaceStateStore.save(persistable);
    if (!saved)
    {
        qCWarning(appControllerLog) << "Unable to persist terminal workspace topology"
                                    << "error=" << static_cast<int>(saved.error());
        return false;
    }
    return true;
}

void AppController::emitActiveTerminalContextChanged()
{
    updateTelemetryVisibility();
    emit activeTerminalTabChanged();
    emit terminalWorkspaceChanged();
    emit remoteTelemetryChanged();
    emit sshActiveChanged();
    emit terminalSearchChanged();
    emit terminalHistoryChanged();
    emit sftpChanged();
    emit aiConversationChanged();
    showActiveTab();
}

void AppController::showActiveTab()
{
    showAllTerminalViewports();
    const TerminalTab *tab = activeTab();
    if (tab != nullptr)
    {
        m_terminal = m_terminalViewports.value(tab->paneId);
    }
    if (m_terminal != nullptr && tab == nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(tr("No terminal session"));
        m_terminal->setKeywordHighlightRules({});
    }
}

void AppController::showTabInViewport(const TerminalTab &tab)
{
    ui::TerminalItem *terminal = m_terminalViewports.value(tab.paneId);
    if (terminal == nullptr)
    {
        return;
    }
    terminal->setSnapshot(tab.snapshot);
    terminal->setStatusText(tab.status);
    terminal->setKeywordHighlightRules(tab.keywordHighlightEnabled ? keywordRulesVariant(tab) : QVariantList{});
    terminal->requestCurrentSize();
}

void AppController::showAllTerminalViewports()
{
    for (const auto &tab : m_tabs)
    {
        showTabInViewport(*tab);
    }
}

QVariantList AppController::keywordRulesVariant(const TerminalTab &tab) const
{
    QVariantList rules;
    rules.reserve(static_cast<qsizetype>(tab.keywordHighlightRules.size()));
    for (const ssh::SshKeywordHighlightRule &rule : tab.keywordHighlightRules)
    {
        rules.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(rule.id)},
            {QStringLiteral("pattern"), utf8QString(rule.pattern)},
            {QStringLiteral("foreground"), utf8QString(rule.foreground)},
            {QStringLiteral("background"), utf8QString(rule.background)},
            {QStringLiteral("enabled"), rule.enabled},
            {QStringLiteral("caseSensitive"), rule.caseSensitive},
        });
    }
    return rules;
}

bool AppController::persistKeywordRules(TerminalTab &tab)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return true;
    }
    std::vector<ssh::SshProfile> candidate = m_profiles;
    const std::string profileId = utf8String(tab.sourceProfileId);
    const auto profile = std::ranges::find(candidate, profileId, &ssh::SshProfile::id);
    if (profile == candidate.end())
    {
        return false;
    }
    profile->keywordHighlightRules = tab.keywordHighlightRules;
    profile->keywordHighlightEnabled = tab.keywordHighlightEnabled;
    if (const auto saved = m_profileStore.save(candidate); !saved)
    {
        return false;
    }
    m_profiles = std::move(candidate);
    emit hostProfilesChanged();
    return true;
}

QVariantList AppController::recordedScriptStepsVariant(const TerminalTab &tab) const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(tab.scriptRecorder.steps().size()));
    for (const workbench::RecordedScriptStep &step : tab.scriptRecorder.steps())
    {
        result.append(step.kind == workbench::RecordedScriptStepKind::Send
                          ? QVariantMap{{QStringLiteral("type"), QStringLiteral("send")},
                                        {QStringLiteral("command"), utf8QString(step.command)}}
                          : QVariantMap{{QStringLiteral("type"), QStringLiteral("delay")},
                                        {QStringLiteral("milliseconds"), step.delayMilliseconds}});
    }
    return result;
}

void AppController::replayRecordedScriptStep(const QString &tabId, const std::size_t index,
                                             const std::uint64_t generation)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || !tab->scriptPlaybackActive || tab->scriptPlaybackGeneration != generation
        || tab->scriptRecorder.state() != workbench::ScriptRecorderState::Review || !tab->running)
    {
        return;
    }
    const auto &steps = tab->scriptRecorder.steps();
    if (index >= steps.size())
    {
        tab->scriptPlaybackActive = false;
        emit terminalTabsChanged();
        return;
    }

    const workbench::RecordedScriptStep &step = steps[index];
    if (step.kind == workbench::RecordedScriptStepKind::Delay)
    {
        const int delay = static_cast<int>(std::min<std::uint32_t>(step.delayMilliseconds, 60'000));
        QTimer::singleShot(delay, this, [this, tabId, index, generation] {
            replayRecordedScriptStep(tabId, index + 1, generation);
        });
        return;
    }

    const QString command = utf8QString(step.command);
    QByteArray bytes = command.toUtf8();
    bytes.replace('\n', '\r');
    if (bytes.isEmpty() || bytes.back() != '\r')
    {
        bytes.append('\r');
    }
    appendCapturedHistory(*tab, command);
    dispatchInput(*tab, bytes);
    QTimer::singleShot(0, this, [this, tabId, index, generation] {
        replayRecordedScriptStep(tabId, index + 1, generation);
    });
}

void AppController::setHostKeyPrompt(QString endpoint, QString algorithm, QString fingerprint, const bool changed)
{
    m_hostKeyEndpoint = std::move(endpoint);
    m_hostKeyAlgorithm = std::move(algorithm);
    m_hostKeyFingerprint = std::move(fingerprint);
    m_hostKeyPromptVisible = true;
    m_hostKeyChangedWarning = changed;
    emit hostKeyPromptChanged();
}

void AppController::clearHostKeyPrompt()
{
    if (!m_hostKeyPromptVisible && m_hostKeyTabId.isEmpty() && m_hostKeyTransferTaskId.isEmpty()
        && m_hostKeyForwardingRuleId.isEmpty() && m_hostKeyEndpoint.isEmpty() && m_hostKeyAlgorithm.isEmpty()
        && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyForSftp = false;
    m_hostKeyTransferTaskId.clear();
    m_hostKeyForwardingRuleId.clear();
    m_hostKeyTabId.clear();
    m_hostKeyEndpoint.clear();
    m_hostKeyAlgorithm.clear();
    m_hostKeyFingerprint.clear();
    emit hostKeyPromptChanged();
}

void AppController::loadPortForwardingRules()
{
    auto rules = m_portForwardingStore.load();
    if (!rules)
    {
        qCWarning(appControllerLog) << "Unable to load port forwarding rules";
        m_portForwardingOperationError = tr("Unable to load the saved port forwarding rules.");
        m_portForwardingRules.clear();
        return;
    }
    m_portForwardingRules = std::move(*rules);
    if (m_portForwardingStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered port forwarding rules from the last-known-good backup";
    }
    QTimer::singleShot(0, this, [this] {
        if (m_shutdownStarted || portableVaultLocked())
        {
            return;
        }
        std::vector<QString> autoStartIds;
        autoStartIds.reserve(m_portForwardingRules.size());
        for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
        {
            if (rule.autoStart)
            {
                autoStartIds.push_back(utf8QString(rule.id));
            }
        }
        for (const QString &id : autoStartIds)
        {
            (void)startPortForwardingRule(id);
        }
    });
}

bool AppController::persistPortForwardingRules(const std::vector<forwarding::PortForwardingRule> &rules)
{
    if (const auto saved = m_portForwardingStore.save(rules); !saved)
    {
        m_portForwardingOperationError = tr("Unable to save the port forwarding rules.");
        emit portForwardingRulesChanged();
        return false;
    }
    return true;
}

void AppController::applyPortForwardingSnapshot(const std::string &ruleId,
                                                const forwarding::PortForwardingJobSnapshot &snapshot)
{
    PortForwardingRuntime *runtime = findPortForwardingRuntime(ruleId);
    if (runtime == nullptr || !runtime->job || runtime->job->snapshot() != snapshot)
    {
        return;
    }
    if (snapshot.state == forwarding::PortForwardingJobState::Failed)
    {
        switch (snapshot.failure)
        {
            case forwarding::PortForwardingJobFailure::Connection:
                m_portForwardingOperationError = tr("The SSH connection for the forwarding rule failed.");
                break;
            case forwarding::PortForwardingJobFailure::Listener:
                m_portForwardingOperationError = tr("The local bind address or port is unavailable.");
                break;
            case forwarding::PortForwardingJobFailure::RemoteListener:
                m_portForwardingOperationError = tr("The SSH server rejected the remote listener.");
                break;
            case forwarding::PortForwardingJobFailure::Transport:
                m_portForwardingOperationError = tr("The forwarding transport was disconnected.");
                break;
            case forwarding::PortForwardingJobFailure::ResourceLimit:
                m_portForwardingOperationError = tr("The forwarding worker exhausted an internal resource.");
                break;
            case forwarding::PortForwardingJobFailure::None:
                break;
        }
    }
    emit portForwardingRulesChanged();
}

AppController::PortForwardingRuntime *AppController::findPortForwardingRuntime(const std::string_view ruleId) noexcept
{
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return std::string_view(candidate->ruleId);
                                           });
    return runtime == m_portForwardingRuntimes.end() ? nullptr : runtime->get();
}

const AppController::PortForwardingRuntime *
AppController::findPortForwardingRuntime(const std::string_view ruleId) const noexcept
{
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return std::string_view(candidate->ruleId);
                                           });
    return runtime == m_portForwardingRuntimes.end() ? nullptr : runtime->get();
}

void AppController::resolvePortForwardingHostKey(PortForwardingRuntime &runtime,
                                                 const ssh::UnknownHostKeyDecision decision) noexcept
{
    {
        std::scoped_lock lock(runtime.hostKeyMutex);
        runtime.hostKeyDecision = decision;
    }
    runtime.hostKeyAvailable.notify_all();
}

void AppController::stopAllPortForwardingRules() noexcept
{
    for (const auto &runtime : m_portForwardingRuntimes)
    {
        resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
        runtime->job->stop();
    }
    m_portForwardingRuntimes.clear();
}

void AppController::loadHostProfiles()
{
    auto profiles = m_profileStore.load();
    if (!profiles)
    {
        qCWarning(appControllerLog) << "Unable to load SSH profiles; starting with an empty profile list";
        return;
    }
    m_profiles = std::move(*profiles);
    if (m_profileStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered SSH profiles from the last-known-good backup";
        recordPersistenceRecovery();
    }
}

void AppController::loadApplicationSettings()
{
    auto settings = m_settingsStore.load();
    if (!settings)
    {
        qCWarning(appControllerLog) << "Unable to load application settings; using defaults";
        return;
    }
    m_settings = std::move(*settings);
    if (m_settingsStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered application settings from the last-known-good backup";
        recordPersistenceRecovery();
    }
    security::CredentialStorage selected = m_defaultCredentialStorage;
    switch (m_settings.credentialStorage)
    {
        case config::CredentialStoragePreference::system:
            selected = security::CredentialStorage::System;
            break;
        case config::CredentialStoragePreference::portable:
            selected = security::CredentialStorage::Portable;
            break;
        case config::CredentialStoragePreference::session:
            selected = security::CredentialStorage::Session;
            break;
        case config::CredentialStoragePreference::automatic:
            break;
    }
    m_credentialVaults->select(selected);
    applyAiNetworkProxy();
}

void AppController::applyAiNetworkProxy()
{
    switch (m_settings.aiProxy)
    {
        case config::AiProxyPreference::system:
            m_aiNetwork.setProxyFactory(new AiSystemProxyFactory());
            return;
        case config::AiProxyPreference::direct:
            m_aiNetwork.setProxy(QNetworkProxy::NoProxy);
            return;
        case config::AiProxyPreference::custom:
            break;
    }

    const QUrl url(m_settings.aiProxyUrl);
    const auto type = url.scheme() == QStringLiteral("socks5") ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy;
    const quint16 defaultPort = type == QNetworkProxy::Socks5Proxy ? quint16{1080} : quint16{8080};
    QString password;
    const ai::AiSecretStore store(m_credentialVaults->active());
    auto secret = store.readApiKey(AiProxyCredentialReference);
    if (secret.has_value())
    {
        const std::string_view value = secret->view();
        password = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }
    m_aiNetwork.setProxy(QNetworkProxy(type, url.host(), static_cast<quint16>(url.port(defaultPort)),
                                       m_settings.aiProxyUsername, password));
}

void AppController::initializeAiModelCatalogRefresh()
{
    m_aiModelRefreshObservedTabCount = m_tabs.size();
    connect(this, &AppController::terminalTabsChanged, this, [this] {
        const std::size_t currentTabCount = m_tabs.size();
        if (currentTabCount > m_aiModelRefreshObservedTabCount)
        {
            refreshConfiguredAiModels();
        }
        m_aiModelRefreshObservedTabCount = currentTabCount;
    });
    QTimer::singleShot(0, this, [this] {
        refreshConfiguredAiModels();
        if (m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt)
        {
            refreshAiChatGptUsageInternal(true);
        }
    });
}

void AppController::refreshConfiguredAiModels()
{
    const bool chatGptSubscription = m_settings.aiProvider == config::AiProviderPreference::openAiChatGpt;
    if (!chatGptSubscription && m_settings.aiBaseUrl.trimmed().isEmpty())
    {
        return;
    }
    if (chatGptSubscription ? !aiChatGptConfigured()
                            : m_settings.aiProvider != config::AiProviderPreference::ollama && !aiApiKeyConfigured())
    {
        return;
    }
    refreshAiModelsInternal(config::aiProviderPreferenceToken(m_settings.aiProvider), m_settings.aiBaseUrl, {}, true);
}

void AppController::loadAiPermissionRules()
{
    auto rules = m_aiPermissionRuleStore.load();
    if (!rules)
    {
        m_aiPermissionRuleError = tr("Assistant permission rules could not be loaded.");
        qCWarning(appControllerLog) << "Unable to load AI permission rules:" << rules.error();
        return;
    }
    if (!m_aiActionToolDispatcher.replacePermissionRules(std::move(*rules)))
    {
        m_aiPermissionRuleError = tr("Assistant permission rules are invalid.");
        qCWarning(appControllerLog) << "Loaded AI permission rules failed validation";
        return;
    }
    if (m_aiPermissionRuleStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered AI permission rules from the last-known-good backup";
        recordPersistenceRecovery();
    }
    m_aiPermissionRuleError.clear();
}

void AppController::loadAiQuickMessages()
{
    auto messages = m_aiQuickMessageStore.load();
    if (!messages)
    {
        m_aiQuickMessageError = tr("Quick messages could not be loaded.");
        qCWarning(appControllerLog) << "Unable to load AI quick messages";
        return;
    }
    m_aiQuickMessages = std::move(*messages);
    m_aiQuickMessageError.clear();
}

bool AppController::replaceAndPersistAiPermissionRules(std::vector<ai::AiPermissionRule> rules)
{
    const std::vector<ai::AiPermissionRule> previous = m_aiActionToolDispatcher.permissionRules();
    if (!m_aiActionToolDispatcher.replacePermissionRules(std::move(rules)))
    {
        m_aiPermissionRuleError = tr("The assistant permission rule is invalid.");
        emit aiPermissionRulesChanged();
        return false;
    }

    std::vector<ai::AiPermissionRule> persistent;
    for (const auto &rule : m_aiActionToolDispatcher.permissionRules())
    {
        if (rule.duration == ai::AiPermissionRuleDuration::profile
            || rule.duration == ai::AiPermissionRuleDuration::global)
        {
            persistent.push_back(rule);
        }
    }
    const auto saved = m_aiPermissionRuleStore.save(persistent);
    if (!saved)
    {
        static_cast<void>(m_aiActionToolDispatcher.replacePermissionRules(previous));
        m_aiPermissionRuleError = tr("Assistant permission rules could not be saved.");
        qCWarning(appControllerLog) << "Unable to save AI permission rules:" << saved.error();
        emit aiPermissionRulesChanged();
        return false;
    }
    m_aiPermissionRuleError.clear();
    emit aiPermissionRulesChanged();
    return true;
}

void AppController::initializeActionRegistry()
{
    m_actionRegistry.setOverrides(m_settings.shortcutOverrides);
    m_settings.shortcutOverrides = m_actionRegistry.overrides();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::actionRegistryChanged);
    QObject::connect(this, &AppController::activeTerminalTabChanged, this, &AppController::actionRegistryChanged);
}

QVariantMap AppController::shortcutResult(const actions::ShortcutValidation &validation) const
{
    QString conflictingLabel;
    if (!validation.conflictingActionId.isEmpty())
    {
        const QVariantList registeredActions = m_actionRegistry.actions(activeTab() != nullptr);
        for (const QVariant &entry : registeredActions)
        {
            const QVariantMap action = entry.toMap();
            if (action.value(QStringLiteral("id")).toString() == validation.conflictingActionId)
            {
                conflictingLabel = action.value(QStringLiteral("label")).toString();
                break;
            }
        }
    }
    return {
        {QStringLiteral("valid"), validation.valid()},
        {QStringLiteral("shortcut"), validation.normalized},
        {QStringLiteral("error"), actions::ActionRegistry::validationMessage(validation.error, conflictingLabel)},
        {QStringLiteral("conflictingActionId"), validation.conflictingActionId},
    };
}

void AppController::loadQuickCommands()
{
    auto scripts = m_scriptStore.loadOrMigrate(m_legacyQuickCommandPath);
    if (!scripts)
    {
        qCWarning(appControllerLog) << "Unable to load scripts; starting with an empty script list";
        setQuickCommandOperationError(tr("Saved scripts could not be loaded."));
        return;
    }
    m_scripts = std::move(*scripts);
    if (m_scriptStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered scripts from the last-known-good backup";
        recordPersistenceRecovery();
    }
}

void AppController::loadWorkspaceState()
{
    auto state = m_workspaceStateStore.load();
    if (!state)
    {
        qCWarning(appControllerLog) << "Unable to load workspace state; using safe defaults";
        return;
    }
    m_workspaceState = std::move(*state);
    if (m_workspaceStateStore.lastLoadRecoveredFromBackup())
    {
        qCWarning(appControllerLog) << "Recovered workspace state from the last-known-good backup";
        recordPersistenceRecovery();
    }
    restoreTerminalWorkspaces();
}

void AppController::recordPersistenceRecovery()
{
    if (!m_startupRecoveryNotice.isEmpty())
    {
        return;
    }
    m_startupRecoveryNotice =
        tr("Some local settings were recovered from a last-known-good backup. Review them before continuing.");
}

void AppController::restoreTerminalWorkspaces()
{
    for (const workbench::TerminalWorkspaceLayout &workspace : m_workspaceState.terminalWorkspaces)
    {
        for (const workbench::TerminalLayoutNode &node : workspace.nodes)
        {
            if (node.kind != workbench::TerminalLayoutNodeKind::Leaf)
            {
                continue;
            }
            const auto intent = std::ranges::find(workspace.restoreIntents, node.restoreIntentId,
                                                  &workbench::TerminalRestoreIntent::id);
            if (intent == workspace.restoreIntents.end() || intent->kind == workbench::TerminalRestoreKind::Transient)
            {
                continue;
            }
            auto tab = std::make_unique<TerminalTab>();
            tab->id = utf8QString(intent->id);
            tab->workspaceId = utf8QString(workspace.id);
            tab->paneId = utf8QString(node.id);
            tab->title = utf8QString(intent->title);
            if (intent->kind == workbench::TerminalRestoreKind::Local)
            {
                tab->kind = TerminalTabKind::Local;
                tab->title = tab->title.isEmpty() ? tr("PowerShell %1").arg(m_nextLocalTabNumber) : tab->title;
                ++m_nextLocalTabNumber;
                tab->status = tr("Restoring local terminal...");
                tab->local = m_localSessionFactory();
                if (!tab->local)
                {
                    continue;
                }
                initializeSessionLog(*tab);
                initializeTerminalOutputSink(*tab);
                connectLocalTabSignals(*tab);
                const QString tabId = tab->id;
                m_tabs.push_back(std::move(tab));
                TerminalTab *created = findTab(tabId);
                if (created != nullptr)
                {
                    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
                    if (error)
                    {
                        created->status =
                            tr("Unable to restore local terminal: %1").arg(QString::fromStdString(error.message()));
                    }
                }
                continue;
            }

            tab->kind = TerminalTabKind::Ssh;
            tab->sourceProfileId = utf8QString(intent->profileId);
            const auto profile = std::ranges::find(m_profiles, intent->profileId, &ssh::SshProfile::id);
            if (profile != m_profiles.end())
            {
                tab->title = tab->title.isEmpty() ? utf8QString(profile->name) : tab->title;
                tab->identity = QStringLiteral("%1@%2:%3")
                                    .arg(utf8QString(profile->username), utf8QString(profile->host))
                                    .arg(profile->port);
                tab->address = utf8QString(profile->host);
                tab->keywordHighlightRules = profile->keywordHighlightRules;
                tab->keywordHighlightEnabled = profile->keywordHighlightEnabled;
                tab->status = tr("SSH workspace restored; reconnect when ready.");
            }
            else
            {
                tab->status = tr("The saved SSH host for this workspace no longer exists.");
            }
            applyWorkspaceState(*tab);
            tab->ssh = std::make_unique<ssh::SshTerminalSession>();
            initializeSessionLog(*tab);
            initializeTerminalOutputSink(*tab);
            connectSshTabSignals(*tab);
            m_tabs.push_back(std::move(tab));
        }
    }

    QString activeWorkspace = utf8QString(m_workspaceState.activeTerminalWorkspaceId);
    if (findTerminalWorkspace(activeWorkspace) == nullptr && !m_workspaceState.terminalWorkspaces.empty())
    {
        activeWorkspace = utf8QString(m_workspaceState.terminalWorkspaces.front().id);
    }
    if (!activeWorkspace.isEmpty())
    {
        static_cast<void>(activateTerminalTab(activeWorkspace));
    }
}

void AppController::applyWorkspaceState(TerminalTab &tab) const
{
    if (tab.sourceProfileId.isEmpty())
    {
        return;
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab.sourceProfileId));
    if (state == nullptr)
    {
        return;
    }
    tab.sftpPath = utf8QString(state->lastRemotePath);
    tab.sftpRequestedPath = tab.sftpPath;
    tab.workbenchPage = utf8QString(state->workbenchPage);
    tab.workbenchSide = utf8QString(state->workbenchSide);
    tab.sftpViewMode = utf8QString(state->sftpViewMode);
    tab.sftpSortColumn = utf8QString(state->sftpSortColumn);
    tab.sftpFilenameEncoding = utf8QString(state->sftpFilenameEncoding);
    tab.followTerminalDirectory = state->followTerminalDirectory;
    tab.sftpSortAscending = state->sftpSortAscending;
    tab.sftpDirectoriesFirst = state->sftpDirectoriesFirst;
    tab.sftpShowModifiedColumn = state->sftpShowModifiedColumn;
    tab.sftpShowSizeColumn = state->sftpShowSizeColumn;
    tab.sftpShowTypeColumn = state->sftpShowTypeColumn;
    tab.workbenchWidth = state->workbenchWidth;
    tab.composerHeight = state->composerHeight;
}

void AppController::persistWorkspaceState(const TerminalTab &tab, const bool shouldRecordRemotePath)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return;
    }
    workbench::WorkspaceState candidate = m_workspaceState;
    workbench::ProfileWorkspaceState &state =
        workbench::ensureProfileWorkspaceState(candidate, utf8String(tab.sourceProfileId));
    if (shouldRecordRemotePath)
    {
        workbench::recordRecentRemotePath(state, utf8String(tab.sftpPath));
    }
    state.workbenchPage = utf8String(tab.workbenchPage);
    state.workbenchSide = utf8String(tab.workbenchSide);
    state.sftpViewMode = utf8String(tab.sftpViewMode);
    state.sftpSortColumn = utf8String(tab.sftpSortColumn);
    state.sftpFilenameEncoding = utf8String(tab.sftpFilenameEncoding);
    state.followTerminalDirectory = tab.followTerminalDirectory;
    state.sftpSortAscending = tab.sftpSortAscending;
    state.sftpDirectoriesFirst = tab.sftpDirectoriesFirst;
    state.sftpShowModifiedColumn = tab.sftpShowModifiedColumn;
    state.sftpShowSizeColumn = tab.sftpShowSizeColumn;
    state.sftpShowTypeColumn = tab.sftpShowTypeColumn;
    state.workbenchWidth = tab.workbenchWidth;
    state.composerHeight = tab.composerHeight;
    if (!saveWorkspaceStateCandidate(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist safe workspace state";
        return;
    }
    m_workspaceState = std::move(candidate);
}

bool AppController::persistApplicationSettings(const config::ApplicationSettings &settings)
{
    if (!m_settingsStore.save(settings))
    {
        qCWarning(appControllerLog) << "Unable to persist application settings";
        return false;
    }
    if (m_settings == settings)
    {
        return true;
    }
    const bool aiDebugTraceChanged = m_settings.aiDebugTraceEnabled != settings.aiDebugTraceEnabled;
    const bool aiProxyChanged = m_settings.aiProxy != settings.aiProxy || m_settings.aiProxyUrl != settings.aiProxyUrl
                                || m_settings.aiProxyUsername != settings.aiProxyUsername;
    m_settings = settings;
    if (aiDebugTraceChanged)
    {
        configureAiDebugTrace();
    }
    if (aiProxyChanged)
    {
        applyAiNetworkProxy();
    }
    for (const auto &tab : m_tabs)
    {
        if (tab->sftpModel != nullptr)
        {
            tab->sftpModel->setShowHidden(m_settings.sftpShowHiddenFiles);
        }
    }
    emit applicationSettingsChanged();
    return true;
}

void AppController::setCredentialOperationError(QString message)
{
    if (m_credentialOperationError == message)
    {
        return;
    }
    m_credentialOperationError = std::move(message);
    emit credentialVaultChanged();
}

void AppController::setQuickCommandOperationError(QString message)
{
    if (m_quickCommandOperationError == message)
    {
        return;
    }
    m_quickCommandOperationError = std::move(message);
    emit quickCommandsChanged();
}

void AppController::setAiQuickMessageError(QString message)
{
    if (m_aiQuickMessageError == message)
    {
        return;
    }
    m_aiQuickMessageError = std::move(message);
    emit aiQuickMessagesChanged();
}

QString AppController::aiUserSkillWarningText(const ai::AiUserSkillWarning warning) const
{
    switch (warning)
    {
        case ai::AiUserSkillWarning::symbolicLink:
            return tr("Skill directories and SKILL.md must be regular local entries.");
        case ai::AiUserSkillWarning::missingSkillFile:
            return tr("SKILL.md is missing.");
        case ai::AiUserSkillWarning::unreadableSkillFile:
            return tr("SKILL.md could not be read.");
        case ai::AiUserSkillWarning::skillFileTooLarge:
            return tr("SKILL.md exceeds the 128 KiB limit.");
        case ai::AiUserSkillWarning::invalidUtf8:
            return tr("SKILL.md must be valid UTF-8 text.");
        case ai::AiUserSkillWarning::missingFrontmatter:
            return tr("SKILL.md needs YAML frontmatter enclosed by --- lines.");
        case ai::AiUserSkillWarning::missingName:
            return tr("The frontmatter name is missing.");
        case ai::AiUserSkillWarning::invalidName:
            return tr("The skill name must use lowercase letters, numbers, and single hyphens.");
        case ai::AiUserSkillWarning::nameDirectoryMismatch:
            return tr("The frontmatter name must match the skill directory name.");
        case ai::AiUserSkillWarning::missingDescription:
            return tr("The frontmatter description is missing.");
        case ai::AiUserSkillWarning::descriptionTooLong:
            return tr("The skill description exceeds 1024 characters.");
        case ai::AiUserSkillWarning::compatibilityTooLong:
            return tr("The compatibility field exceeds 500 characters.");
        case ai::AiUserSkillWarning::emptyInstructions:
            return tr("The skill has no Markdown instructions after its frontmatter.");
        case ai::AiUserSkillWarning::disallowedControl:
            return tr("The skill contains unsupported control characters.");
        case ai::AiUserSkillWarning::catalogueLimit:
            return tr("Only the first 200 skill directories are loaded.");
    }
    return tr("The skill could not be loaded.");
}

void AppController::setNoteOperationError(QString message)
{
    if (m_noteOperationError == message)
    {
        return;
    }
    m_noteOperationError = std::move(message);
    emit notesChanged();
}

} // namespace ztermy
