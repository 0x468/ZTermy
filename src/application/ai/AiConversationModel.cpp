#include "application/ai/AiConversationModel.h"

#include "domain/ai/AiProviderReplayCodec.h"
#include "domain/ai/AiToolEvidence.h"

#include <QByteArray>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr std::size_t maximumWebSourcesPerMessage = 24;
constexpr std::size_t maximumWebSourceUrlBytes = std::size_t{8} * 1024;
constexpr std::size_t maximumWebSourceTitleBytes = 1024;
constexpr std::size_t maximumWebSourceCitationBytes = std::size_t{4} * 1024;
constexpr std::size_t maximumToolActivitiesPerMessage = 32;
constexpr std::size_t maximumToolActivityIdBytes = 256;
constexpr std::size_t maximumToolActivityNameBytes = 128;
constexpr std::size_t maximumToolActivitySummaryBytes = 4096;
constexpr std::size_t maximumToolActivityArgumentsBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumToolActivityResultBytes = std::size_t{24} * 1024;
constexpr std::size_t maximumToolActivityStateBytes = 64;
constexpr std::size_t maximumToolActivityResultCodeBytes = 128;
constexpr std::size_t maximumCostCatalogDateBytes = 64;

[[nodiscard]] std::size_t imageStorageBytes(const std::span<const AiImageAttachment> images) noexcept
{
    std::size_t bytes = 0;
    for (const auto &image : images)
    {
        bytes += image.id.size() + image.fileName.size() + image.mediaType.size() + image.base64Data.size()
                 + image.previewBase64Data.size();
    }
    return bytes;
}

[[nodiscard]] QString historicalImageMarker(const std::span<const AiImageAttachment> images)
{
    QStringList lines;
    lines.reserve(static_cast<qsizetype>(images.size()));
    for (const auto &image : images)
    {
        lines.push_back(QStringLiteral("[Historical image attachment omitted from replay: %1 · %2 · %3 bytes]")
                            .arg(QString::fromUtf8(image.fileName), QString::fromUtf8(image.mediaType))
                            .arg(image.byteSize));
    }
    return lines.join(QLatin1Char('\n'));
}

[[nodiscard]] QString replayText(const QString &text, const std::span<const AiImageAttachment> images)
{
    if (images.empty())
    {
        return text;
    }
    const QString marker = historicalImageMarker(images);
    return text.isEmpty() ? marker : text + QStringLiteral("\n\n") + marker;
}

[[nodiscard]] QVariantList imageValues(const std::span<const AiImageAttachment> images)
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(images.size()));
    for (const auto &image : images)
    {
        values.push_back(QVariantMap{
            {QStringLiteral("id"), QString::fromUtf8(image.id)},
            {QStringLiteral("fileName"), QString::fromUtf8(image.fileName)},
            {QStringLiteral("mediaType"), QString::fromUtf8(image.mediaType)},
            {QStringLiteral("byteSize"), QVariant::fromValue<qulonglong>(image.byteSize)},
            {QStringLiteral("pixelWidth"), image.pixelWidth},
            {QStringLiteral("pixelHeight"), image.pixelHeight},
            {QStringLiteral("previewUrl"),
             QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(image.previewBase64Data))},
        });
    }
    return values;
}

[[nodiscard]] std::size_t sourceStorageBytes(const std::span<const AiWebSource> sources) noexcept
{
    std::size_t bytes = 0;
    for (const auto &source : sources)
    {
        bytes += source.url.size() + source.title.size() + source.citedText.size();
    }
    return bytes;
}

[[nodiscard]] QVariantList sourceValues(const std::span<const AiWebSource> sources)
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(sources.size()));
    for (const auto &source : sources)
    {
        values.push_back(QVariantMap{{QStringLiteral("url"), QString::fromUtf8(source.url)},
                                     {QStringLiteral("title"), QString::fromUtf8(source.title)},
                                     {QStringLiteral("citedText"), QString::fromUtf8(source.citedText)}});
    }
    return values;
}

[[nodiscard]] std::size_t toolActivityStorageBytes(const std::span<const AiToolActivity> activities) noexcept
{
    std::size_t bytes = 0;
    for (const auto &activity : activities)
    {
        bytes += activity.id.size() + activity.name.size() + activity.summary.size() + activity.argumentsJson.size()
                 + activity.resultJson.size() + activity.state.size() + activity.resultCode.size();
    }
    return bytes;
}

[[nodiscard]] QVariantList toolActivityValues(const std::span<const AiToolActivity> activities)
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(activities.size()));
    for (const auto &activity : activities)
    {
        values.push_back(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(activity.id)},
                                     {QStringLiteral("name"), QString::fromUtf8(activity.name)},
                                     {QStringLiteral("summary"), QString::fromUtf8(activity.summary)},
                                     {QStringLiteral("argumentsJson"), QString::fromUtf8(activity.argumentsJson)},
                                     {QStringLiteral("resultJson"), QString::fromUtf8(activity.resultJson)},
                                     {QStringLiteral("state"), QString::fromUtf8(activity.state)},
                                     {QStringLiteral("resultCode"), QString::fromUtf8(activity.resultCode)},
                                     {QStringLiteral("sideEffecting"), activity.sideEffecting},
                                     {QStringLiteral("highRisk"), activity.highRisk}});
    }
    return values;
}

[[nodiscard]] bool validToolActivity(const AiToolActivity &activity) noexcept
{
    return !activity.id.empty() && activity.id.size() <= maximumToolActivityIdBytes && !activity.name.empty()
           && activity.name.size() <= maximumToolActivityNameBytes
           && activity.summary.size() <= maximumToolActivitySummaryBytes
           && activity.argumentsJson.size() <= maximumToolActivityArgumentsBytes
           && activity.resultJson.size() <= maximumToolActivityResultBytes
           && activity.state.size() <= maximumToolActivityStateBytes
           && activity.resultCode.size() <= maximumToolActivityResultCodeBytes;
}

[[nodiscard]] bool validWebSourceUrl(const std::string &url)
{
    if (url.empty() || url.size() > maximumWebSourceUrlBytes)
    {
        return false;
    }
    const QUrl value = QUrl::fromEncoded(QByteArray(url.data(), static_cast<qsizetype>(url.size())));
    return value.isValid() && (value.scheme() == QStringLiteral("https") || value.scheme() == QStringLiteral("http"));
}

} // namespace

AiConversationModel::AiConversationModel(AiConversationLimits limits, QObject *parent)
    : QAbstractListModel(parent), m_limits(limits)
{
    m_limits.maxMessages = std::max<std::size_t>(2, m_limits.maxMessages);
    m_limits.maxMessageBytes = std::max<std::size_t>(1, m_limits.maxMessageBytes);
    m_limits.maxConversationBytes = std::max(m_limits.maxMessageBytes, m_limits.maxConversationBytes);
}

int AiConversationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0
                            : static_cast<int>(std::min<std::size_t>(
                                  m_messages.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

QVariant AiConversationModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= m_messages.size())
    {
        return {};
    }
    const auto &message = m_messages[static_cast<std::size_t>(index.row())];
    switch (role)
    {
        case MessageIdRole:
            return QVariant::fromValue<qulonglong>(message.id);
        case MessageRole:
            return roleToken(message.role);
        case TextRole:
            return message.text;
        case ReasoningRole:
            return message.reasoning;
        case StateRole:
            return stateToken(message.state);
        case ErrorRole:
            return message.error;
        case TruncatedRole:
            return message.truncated;
        case InputTokensRole:
            return QVariant::fromValue<qulonglong>(message.usage.has_value() ? message.usage->inputTokens : 0);
        case OutputTokensRole:
            return QVariant::fromValue<qulonglong>(message.usage.has_value() ? message.usage->outputTokens : 0);
        case CachedInputTokensRole:
            return QVariant::fromValue<qulonglong>(message.usage.has_value() ? message.usage->cachedInputTokens : 0);
        case ReasoningTokensRole:
            return QVariant::fromValue<qulonglong>(message.usage.has_value() ? message.usage->reasoningTokens : 0);
        case UsageAvailableRole:
            return message.usage.has_value();
        case WallTimeMillisecondsRole:
            return QVariant::fromValue<qulonglong>(message.metrics.has_value() ? message.metrics->wallTimeMilliseconds
                                                                               : 0);
        case FirstTokenMillisecondsRole:
            return QVariant::fromValue<qlonglong>(message.metrics.has_value()
                                                          && message.metrics->firstTokenMilliseconds.has_value()
                                                      ? static_cast<qlonglong>(*message.metrics->firstTokenMilliseconds)
                                                      : qlonglong{-1});
        case RetryCountRole:
            return message.metrics.has_value() ? message.metrics->retryCount : 0;
        case EstimatedCostKnownRole:
            return message.estimatedCostUsd.has_value();
        case EstimatedCostUsdRole:
            return message.estimatedCostUsd.value_or(0.0);
        case CostCatalogDateRole:
            return message.costCatalogDate;
        case LongContextRatesRole:
            return message.longContextRates;
        case CommandSuggestionRole:
            return message.commandSuggestion;
        case HasCommandSuggestionRole:
            return !message.commandSuggestion.isEmpty();
        case ToolActivitiesRole:
            return toolActivityValues(message.toolActivities);
        case ImageAttachmentsRole:
            return imageValues(message.images);
        case SourcesRole:
            return sourceValues(message.sources);
        case ToolEvidenceStateRole:
        {
            const auto verdict = evaluateToolEvidence(message.toolActivities);
            const std::string_view token = aiToolEvidenceStateToken(verdict.state);
            return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
        }
        case ToolEvidenceFailedCountRole:
            return evaluateToolEvidence(message.toolActivities).failedCount;
        case ToolEvidencePendingCountRole:
            return evaluateToolEvidence(message.toolActivities).pendingCount;
        case ToolEvidenceFailedSideEffectCountRole:
            return evaluateToolEvidence(message.toolActivities).failedSideEffectCount;
        default:
            return {};
    }
}

QHash<int, QByteArray> AiConversationModel::roleNames() const
{
    return {{MessageIdRole, "messageId"},
            {MessageRole, "messageRole"},
            {TextRole, "text"},
            {ReasoningRole, "reasoning"},
            {StateRole, "state"},
            {ErrorRole, "error"},
            {TruncatedRole, "truncated"},
            {InputTokensRole, "inputTokens"},
            {OutputTokensRole, "outputTokens"},
            {CachedInputTokensRole, "cachedInputTokens"},
            {ReasoningTokensRole, "reasoningTokens"},
            {UsageAvailableRole, "usageAvailable"},
            {WallTimeMillisecondsRole, "wallTimeMilliseconds"},
            {FirstTokenMillisecondsRole, "firstTokenMilliseconds"},
            {RetryCountRole, "retryCount"},
            {EstimatedCostKnownRole, "estimatedCostKnown"},
            {EstimatedCostUsdRole, "estimatedCostUsd"},
            {CostCatalogDateRole, "costCatalogDate"},
            {LongContextRatesRole, "longContextRates"},
            {CommandSuggestionRole, "commandSuggestion"},
            {HasCommandSuggestionRole, "hasCommandSuggestion"},
            {ToolActivitiesRole, "toolActivities"},
            {ImageAttachmentsRole, "imageAttachments"},
            {SourcesRole, "sources"},
            {ToolEvidenceStateRole, "toolEvidenceState"},
            {ToolEvidenceFailedCountRole, "toolEvidenceFailedCount"},
            {ToolEvidencePendingCountRole, "toolEvidencePendingCount"},
            {ToolEvidenceFailedSideEffectCountRole, "toolEvidenceFailedSideEffectCount"}};
}

bool AiConversationModel::streaming() const noexcept
{
    return m_streaming;
}

const AiConversationLimits &AiConversationModel::limits() const noexcept
{
    return m_limits;
}

std::vector<AiChatMessage> AiConversationModel::providerMessages() const
{
    std::vector<AiChatMessage> messages;
    messages.reserve(m_messages.size());
    std::uint64_t latestUserMessageId = 0;
    for (auto iterator = m_messages.rbegin(); iterator != m_messages.rend(); ++iterator)
    {
        if (iterator->role == AiMessageRole::user && iterator->state == MessageState::complete)
        {
            latestUserMessageId = iterator->id;
            break;
        }
    }
    for (const auto &message : m_messages)
    {
        if (message.state == MessageState::streaming || message.state == MessageState::failed
            || message.state == MessageState::cancelled)
        {
            continue;
        }
        const bool includeImages = message.id == latestUserMessageId;
        const QString content = includeImages ? message.text : replayText(message.text, message.images);
        messages.push_back(AiChatMessage{.role = message.role,
                                         .content = content.toUtf8().toStdString(),
                                         .images = includeImages ? message.images : std::vector<AiImageAttachment>{},
                                         .providerReplayJson = message.providerReplayJson});
    }
    return messages;
}

std::vector<AiConversationTranscriptEntry> AiConversationModel::transcript() const
{
    std::vector<AiConversationTranscriptEntry> entries;
    entries.reserve(m_messages.size() + m_evidenceMessages.size());
    for (const auto &message : m_messages)
    {
        if (message.state != MessageState::streaming && message.state != MessageState::failed
            && message.state != MessageState::cancelled)
        {
            entries.push_back({.role = message.role == AiMessageRole::user ? AiConversationTranscriptRole::user
                                                                           : AiConversationTranscriptRole::assistant,
                               .content = replayText(message.text, message.images).toUtf8().toStdString(),
                               .reasoning = message.reasoning.toUtf8().toStdString(),
                               .toolActivities = message.toolActivities,
                               .sources = message.sources,
                               .providerReplayJson = message.providerReplayJson,
                               .usage = message.usage,
                               .metrics = message.metrics,
                               .estimatedCostUsd = message.estimatedCostUsd,
                               .costCatalogDate = message.costCatalogDate.toUtf8().toStdString(),
                               .longContextRates = message.longContextRates,
                               .truncated = message.truncated});
        }
        for (const auto &evidence : m_evidenceMessages)
        {
            if (evidence.afterMessageId == message.id)
            {
                entries.push_back(
                    {.role = AiConversationTranscriptRole::evidence, .content = evidence.text.toUtf8().toStdString()});
            }
        }
    }
    return entries;
}

std::vector<AiChatMessage> AiConversationModel::providerMessagesWithEvidence() const
{
    std::vector<AiChatMessage> messages;
    messages.reserve(m_messages.size() + m_evidenceMessages.size());
    std::uint64_t latestUserMessageId = 0;
    for (auto iterator = m_messages.rbegin(); iterator != m_messages.rend(); ++iterator)
    {
        if (iterator->role == AiMessageRole::user && iterator->state == MessageState::complete)
        {
            latestUserMessageId = iterator->id;
            break;
        }
    }
    for (const auto &message : m_messages)
    {
        if (message.state == MessageState::streaming || message.state == MessageState::failed
            || message.state == MessageState::cancelled)
        {
            continue;
        }
        const bool includeImages = message.id == latestUserMessageId;
        const QString content = includeImages ? message.text : replayText(message.text, message.images);
        messages.push_back({.role = message.role,
                            .content = content.toUtf8().toStdString(),
                            .images = includeImages ? message.images : std::vector<AiImageAttachment>{},
                            .providerReplayJson = message.providerReplayJson});
        for (const auto &evidence : m_evidenceMessages)
        {
            if (evidence.afterMessageId == message.id)
            {
                messages.push_back({.role = AiMessageRole::user, .content = evidence.text.toUtf8().toStdString()});
            }
        }
    }
    return messages;
}

bool AiConversationModel::restoreTranscript(const std::vector<AiConversationTranscriptEntry> &entries)
{
    std::size_t messageCount = 0;
    std::size_t evidenceCount = 0;
    std::size_t messageBytes = 0;
    std::size_t evidenceBytes = 0;
    bool hasEvidenceAnchor = false;
    for (const auto &entry : entries)
    {
        const auto replay = AiProviderReplayCodec::decode(entry.providerReplayJson);
        const std::size_t storedBytes =
            entry.content.size() + entry.reasoning.size() + toolActivityStorageBytes(entry.toolActivities)
            + sourceStorageBytes(entry.sources) + entry.providerReplayJson.size() + entry.costCatalogDate.size();
        const bool assistant = entry.role == AiConversationTranscriptRole::assistant;
        if ((entry.content.empty() && !assistant) || storedBytes > m_limits.maxMessageBytes
            || (!assistant
                && (!entry.reasoning.empty() || !entry.toolActivities.empty() || !entry.sources.empty()
                    || !entry.providerReplayJson.empty() || entry.usage.has_value() || entry.metrics.has_value()
                    || entry.estimatedCostUsd.has_value() || !entry.costCatalogDate.empty() || entry.longContextRates
                    || entry.truncated))
            || entry.toolActivities.size() > maximumToolActivitiesPerMessage
            || !std::ranges::all_of(entry.toolActivities, validToolActivity)
            || entry.costCatalogDate.size() > maximumCostCatalogDateBytes
            || (entry.estimatedCostUsd.has_value()
                && (!std::isfinite(*entry.estimatedCostUsd) || *entry.estimatedCostUsd < 0.0))
            || entry.content.size() + sourceStorageBytes(entry.sources) + entry.providerReplayJson.size()
                   > m_limits.maxMessageBytes
            || (!entry.providerReplayJson.empty() && !replay.has_value())
            || entry.sources.size() > maximumWebSourcesPerMessage
            || !std::ranges::all_of(entry.sources, [](const AiWebSource &source) {
                   return validWebSourceUrl(source.url) && source.title.size() <= maximumWebSourceTitleBytes
                          && source.citedText.size() <= maximumWebSourceCitationBytes;
               }))
        {
            return false;
        }
        if (entry.role == AiConversationTranscriptRole::evidence)
        {
            if (!hasEvidenceAnchor)
            {
                return false;
            }
            ++evidenceCount;
            evidenceBytes += entry.content.size();
            if (evidenceCount > m_limits.maxMessages || evidenceBytes > m_limits.maxConversationBytes)
            {
                return false;
            }
        }
        else
        {
            hasEvidenceAnchor = true;
            ++messageCount;
            messageBytes += storedBytes;
            if (messageCount > m_limits.maxMessages || messageBytes > m_limits.maxConversationBytes)
            {
                return false;
            }
        }
    }

    clear();
    for (const auto &entry : entries)
    {
        if (entry.role == AiConversationTranscriptRole::user)
        {
            static_cast<void>(appendUserMessage(QString::fromUtf8(entry.content)));
            continue;
        }
        if (entry.role == AiConversationTranscriptRole::evidence)
        {
            if (!appendEvidenceMessage(QString::fromUtf8(entry.content)))
            {
                clear();
                return false;
            }
            continue;
        }
        const std::uint64_t messageId = beginAssistantMessage();
        const auto replay = AiProviderReplayCodec::decode(entry.providerReplayJson);
        if ((!entry.content.empty() && !appendAssistantDelta(messageId, QString::fromUtf8(entry.content)))
            || (!entry.reasoning.empty()
                && !appendAssistantReasoningDelta(messageId, QString::fromUtf8(entry.reasoning)))
            || !std::ranges::all_of(entry.toolActivities,
                                    [this, messageId](const AiToolActivity &activity) {
                                        const QString callId = QString::fromUtf8(activity.id);
                                        return upsertAssistantToolActivity(messageId, callId,
                                                                           QString::fromUtf8(activity.name),
                                                                           QString::fromUtf8(activity.summary),
                                                                           QString::fromUtf8(activity.state),
                                                                           QString::fromUtf8(activity.resultCode),
                                                                           activity.sideEffecting, activity.highRisk)
                                               && setAssistantToolDetails(messageId, callId,
                                                                          QString::fromUtf8(activity.argumentsJson),
                                                                          QString::fromUtf8(activity.resultJson));
                                    })
            || !std::ranges::all_of(entry.sources,
                                    [this, messageId](const AiWebSource &source) {
                                        return appendAssistantSource(messageId, source);
                                    })
            || (!entry.providerReplayJson.empty()
                && !setAssistantProviderReplay(messageId, replay->toolHistory, replay->finalAssistantContentJson))
            || !completeAssistantMessage(messageId, entry.usage)
            || ((entry.metrics.has_value() || entry.estimatedCostUsd.has_value())
                && !setAssistantMetrics(messageId, entry.metrics.value_or(AiTurnMetrics{}),
                                        AiCostEstimate{.usd = entry.estimatedCostUsd,
                                                       .catalogDate = entry.costCatalogDate,
                                                       .longContextRatesApplied = entry.longContextRates})))
        {
            clear();
            return false;
        }
        if (entry.truncated)
        {
            Message *message = find(messageId);
            Q_ASSERT(message != nullptr);
            message->truncated = true;
            const auto row = indexOf(messageId);
            emit dataChanged(index(row), index(row), {TruncatedRole});
        }
    }
    return true;
}

bool AiConversationModel::appendEvidenceMessage(QString text)
{
    bool truncated = false;
    text = boundedUtf8(std::move(text), m_limits.maxMessageBytes, truncated);
    if (text.trimmed().isEmpty() || m_messages.empty())
    {
        return false;
    }
    const auto bytes = static_cast<std::size_t>(text.toUtf8().size());
    while (!m_evidenceMessages.empty()
           && (m_evidenceMessages.size() >= m_limits.maxMessages
               || m_evidenceBytes + bytes > m_limits.maxConversationBytes))
    {
        m_evidenceBytes -= m_evidenceMessages.front().bytes;
        m_evidenceMessages.erase(m_evidenceMessages.begin());
    }
    if (bytes > m_limits.maxConversationBytes)
    {
        return false;
    }
    m_evidenceBytes += bytes;
    m_evidenceMessages.push_back({.afterMessageId = m_messages.back().id, .text = std::move(text), .bytes = bytes});
    return true;
}

std::uint64_t AiConversationModel::appendUserMessage(QString text, std::vector<AiImageAttachment> images)
{
    bool truncated = false;
    text = boundedUtf8(std::move(text), m_limits.maxMessageBytes, truncated);
    const auto bytes = static_cast<std::size_t>(text.toUtf8().size()) + imageStorageBytes(images);
    evictFor(bytes);
    const auto row = rowCount();
    beginInsertRows({}, row, row);
    const auto id = m_nextMessageId++;
    m_messages.push_back(Message{.id = id,
                                 .role = AiMessageRole::user,
                                 .text = std::move(text),
                                 .bytes = bytes,
                                 .truncated = truncated,
                                 .images = std::move(images)});
    m_totalBytes += bytes;
    endInsertRows();
    emit countChanged();
    return id;
}

std::uint64_t AiConversationModel::beginAssistantMessage()
{
    evictFor(0);
    const auto row = rowCount();
    beginInsertRows({}, row, row);
    const auto id = m_nextMessageId++;
    m_messages.push_back(Message{.id = id, .role = AiMessageRole::assistant, .state = MessageState::streaming});
    endInsertRows();
    emit countChanged();
    updateStreaming();
    return id;
}

bool AiConversationModel::appendAssistantDelta(const std::uint64_t messageId, QString delta)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || message->state != MessageState::streaming
        || delta.isEmpty())
    {
        return false;
    }
    const auto availableForMessage = m_limits.maxMessageBytes - std::min(message->bytes, m_limits.maxMessageBytes);
    const auto availableForConversation =
        m_limits.maxConversationBytes - std::min(m_totalBytes, m_limits.maxConversationBytes);
    const auto available = std::min(availableForMessage, availableForConversation);
    bool truncated = false;
    delta = boundedUtf8(std::move(delta), available, truncated);
    const auto addedBytes = static_cast<std::size_t>(delta.toUtf8().size());
    message->text += delta;
    message->bytes += addedBytes;
    message->truncated = message->truncated || truncated;
    m_totalBytes += addedBytes;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {TextRole, TruncatedRole});
    return addedBytes > 0 || truncated;
}

bool AiConversationModel::appendAssistantReasoningDelta(const std::uint64_t messageId, QString delta)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || message->state != MessageState::streaming
        || delta.isEmpty())
    {
        return false;
    }
    const auto availableForMessage = m_limits.maxMessageBytes - std::min(message->bytes, m_limits.maxMessageBytes);
    const auto availableForConversation =
        m_limits.maxConversationBytes - std::min(m_totalBytes, m_limits.maxConversationBytes);
    const auto available = std::min(availableForMessage, availableForConversation);
    bool truncated = false;
    delta = boundedUtf8(std::move(delta), available, truncated);
    const auto addedBytes = static_cast<std::size_t>(delta.toUtf8().size());
    message->reasoning += delta;
    message->bytes += addedBytes;
    message->truncated = message->truncated || truncated;
    m_totalBytes += addedBytes;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {ReasoningRole, TruncatedRole});
    return addedBytes > 0 || truncated;
}

bool AiConversationModel::appendAssistantSource(const std::uint64_t messageId, AiWebSource source)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || !validWebSourceUrl(source.url))
    {
        return false;
    }
    bool truncated = false;
    QString title = boundedUtf8(QString::fromUtf8(source.title), maximumWebSourceTitleBytes, truncated);
    QString citedText = boundedUtf8(QString::fromUtf8(source.citedText), maximumWebSourceCitationBytes, truncated);
    source.title = title.toUtf8().toStdString();
    source.citedText = citedText.toUtf8().toStdString();

    const auto existing = std::ranges::find(message->sources, source.url, &AiWebSource::url);
    if (existing != message->sources.end())
    {
        std::size_t addedBytes = 0;
        if (existing->title.empty() && !source.title.empty())
        {
            addedBytes += source.title.size();
        }
        if (existing->citedText.empty() && !source.citedText.empty())
        {
            addedBytes += source.citedText.size();
        }
        const auto availableForMessage = m_limits.maxMessageBytes - std::min(message->bytes, m_limits.maxMessageBytes);
        const auto availableForConversation =
            m_limits.maxConversationBytes - std::min(m_totalBytes, m_limits.maxConversationBytes);
        if (addedBytes > std::min(availableForMessage, availableForConversation))
        {
            message->truncated = true;
            const auto row = indexOf(messageId);
            emit dataChanged(index(row), index(row), {TruncatedRole});
            return false;
        }
        if (existing->title.empty())
        {
            existing->title = std::move(source.title);
        }
        if (existing->citedText.empty())
        {
            existing->citedText = std::move(source.citedText);
        }
        message->bytes += addedBytes;
        m_totalBytes += addedBytes;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {SourcesRole});
        return true;
    }
    if (message->sources.size() >= maximumWebSourcesPerMessage)
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }
    const auto addedBytes = source.url.size() + source.title.size() + source.citedText.size();
    const auto availableForMessage = m_limits.maxMessageBytes - std::min(message->bytes, m_limits.maxMessageBytes);
    const auto availableForConversation =
        m_limits.maxConversationBytes - std::min(m_totalBytes, m_limits.maxConversationBytes);
    if (addedBytes > std::min(availableForMessage, availableForConversation))
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }
    message->sources.push_back(std::move(source));
    message->bytes += addedBytes;
    m_totalBytes += addedBytes;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {SourcesRole});
    return true;
}

bool AiConversationModel::upsertAssistantToolActivity(const std::uint64_t messageId, QString toolCallId,
                                                      QString toolName, QString summary, QString state,
                                                      QString resultCode, const bool sideEffecting, const bool highRisk)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || toolCallId.isEmpty() || toolName.isEmpty())
    {
        return false;
    }
    bool truncated = false;
    toolCallId = boundedUtf8(std::move(toolCallId), 256, truncated);
    toolName = boundedUtf8(std::move(toolName), 128, truncated);
    summary = boundedUtf8(std::move(summary), 4096, truncated);
    state = boundedUtf8(std::move(state), 64, truncated);
    resultCode = boundedUtf8(std::move(resultCode), 128, truncated);
    const std::string activityId = toolCallId.toUtf8().toStdString();
    const auto existing = std::ranges::find(message->toolActivities, activityId, &AiToolActivity::id);
    AiToolActivity activity{
        .id = activityId,
        .name = toolName.toUtf8().toStdString(),
        .summary = summary.toUtf8().toStdString(),
        .argumentsJson = existing == message->toolActivities.end() ? std::string{} : existing->argumentsJson,
        .resultJson = existing == message->toolActivities.end() ? std::string{} : existing->resultJson,
        .state = state.toUtf8().toStdString(),
        .resultCode = resultCode.toUtf8().toStdString(),
        .sideEffecting = sideEffecting,
        .highRisk = highRisk};
    const std::size_t previousBytes =
        existing == message->toolActivities.end() ? 0 : toolActivityStorageBytes(std::span(&*existing, 1));
    const std::size_t activityBytes = toolActivityStorageBytes(std::span(&activity, 1));
    const std::size_t evictedBytes =
        existing == message->toolActivities.end() && message->toolActivities.size() >= maximumToolActivitiesPerMessage
            ? toolActivityStorageBytes(std::span(message->toolActivities.data(), 1))
            : 0;
    const auto availableForMessage =
        m_limits.maxMessageBytes
        - std::min(message->bytes - std::min(message->bytes, previousBytes + evictedBytes), m_limits.maxMessageBytes);
    const auto availableForConversation =
        m_limits.maxConversationBytes
        - std::min(m_totalBytes - std::min(m_totalBytes, previousBytes + evictedBytes), m_limits.maxConversationBytes);
    if (activityBytes > std::min(availableForMessage, availableForConversation))
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }
    if (existing != message->toolActivities.end())
    {
        *existing = std::move(activity);
    }
    else
    {
        if (message->toolActivities.size() >= maximumToolActivitiesPerMessage)
        {
            message->toolActivities.erase(message->toolActivities.begin());
            message->bytes -= std::min(message->bytes, evictedBytes);
            m_totalBytes -= std::min(m_totalBytes, evictedBytes);
        }
        message->toolActivities.push_back(std::move(activity));
    }
    message->bytes = message->bytes - std::min(message->bytes, previousBytes) + activityBytes;
    m_totalBytes = m_totalBytes - std::min(m_totalBytes, previousBytes) + activityBytes;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row),
                     {ToolActivitiesRole, ToolEvidenceStateRole, ToolEvidenceFailedCountRole,
                      ToolEvidencePendingCountRole, ToolEvidenceFailedSideEffectCountRole});
    return true;
}

bool AiConversationModel::setAssistantToolDetails(const std::uint64_t messageId, const QString &toolCallId,
                                                  QString argumentsJson, QString resultJson)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || toolCallId.isEmpty())
    {
        return false;
    }
    const QByteArray id = toolCallId.toUtf8();
    const auto existing = std::ranges::find(message->toolActivities, id.toStdString(), &AiToolActivity::id);
    if (existing == message->toolActivities.end())
    {
        return false;
    }
    AiToolActivity updated = *existing;
    bool argumentsTruncated = false;
    bool resultTruncated = false;
    if (!argumentsJson.isNull())
    {
        argumentsJson = boundedUtf8(std::move(argumentsJson), maximumToolActivityArgumentsBytes, argumentsTruncated);
        updated.argumentsJson = argumentsJson.toUtf8().toStdString();
    }
    if (!resultJson.isNull())
    {
        resultJson = boundedUtf8(std::move(resultJson), maximumToolActivityResultBytes, resultTruncated);
        updated.resultJson = resultJson.toUtf8().toStdString();
    }
    const std::size_t previousBytes = toolActivityStorageBytes(std::span(&*existing, 1));
    const std::size_t updatedBytes = toolActivityStorageBytes(std::span(&updated, 1));
    const std::size_t messageWithoutActivity = message->bytes - std::min(message->bytes, previousBytes);
    const std::size_t conversationWithoutActivity = m_totalBytes - std::min(m_totalBytes, previousBytes);
    if (messageWithoutActivity + updatedBytes > m_limits.maxMessageBytes
        || conversationWithoutActivity + updatedBytes > m_limits.maxConversationBytes)
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }
    *existing = std::move(updated);
    message->bytes = messageWithoutActivity + updatedBytes;
    m_totalBytes = conversationWithoutActivity + updatedBytes;
    message->truncated = message->truncated || argumentsTruncated || resultTruncated;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {ToolActivitiesRole, TruncatedRole});
    return true;
}

bool AiConversationModel::setAssistantProviderReplay(const std::uint64_t messageId,
                                                     const std::span<const AiToolExchange> toolHistory,
                                                     const std::string_view finalAssistantContentJson)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant || message->state != MessageState::streaming)
    {
        return false;
    }
    const auto encoded = AiProviderReplayCodec::encode(toolHistory, finalAssistantContentJson);
    if (!encoded.has_value())
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }

    const std::size_t previousBytes = message->providerReplayJson.size();
    const std::size_t messageWithoutReplay = message->bytes - std::min(message->bytes, previousBytes);
    if (messageWithoutReplay + encoded->size() > m_limits.maxMessageBytes
        || encoded->size() > m_limits.maxConversationBytes)
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }

    while (m_totalBytes - std::min(m_totalBytes, previousBytes) + encoded->size() > m_limits.maxConversationBytes
           && discardOldestProviderReplay(messageId))
    {
    }
    if (m_totalBytes - std::min(m_totalBytes, previousBytes) + encoded->size() > m_limits.maxConversationBytes)
    {
        message->truncated = true;
        const auto row = indexOf(messageId);
        emit dataChanged(index(row), index(row), {TruncatedRole});
        return false;
    }
    message->bytes = messageWithoutReplay + encoded->size();
    m_totalBytes = m_totalBytes - std::min(m_totalBytes, previousBytes) + encoded->size();
    message->providerReplayJson = *encoded;
    return true;
}

bool AiConversationModel::completeAssistantMessage(const std::uint64_t messageId, std::optional<AiTokenUsage> usage)
{
    auto *message = find(messageId);
    if (message == nullptr || message->state != MessageState::streaming)
    {
        return false;
    }
    message->state = MessageState::complete;
    message->usage = usage;
    message->commandSuggestion = commandSuggestion(message->text);
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row),
                     {StateRole, InputTokensRole, OutputTokensRole, CachedInputTokensRole, ReasoningTokensRole,
                      UsageAvailableRole, CommandSuggestionRole, HasCommandSuggestionRole});
    updateStreaming();
    return true;
}

bool AiConversationModel::updateAssistantUsage(const std::uint64_t messageId, const AiTokenUsage &usage)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant)
    {
        return false;
    }
    message->usage = usage;
    const auto row = indexOf(messageId);
    emit dataChanged(
        index(row), index(row),
        {InputTokensRole, OutputTokensRole, CachedInputTokensRole, ReasoningTokensRole, UsageAvailableRole});
    return true;
}

bool AiConversationModel::setAssistantMetrics(const std::uint64_t messageId, const AiTurnMetrics &metrics,
                                              const AiCostEstimate &costEstimate)
{
    auto *message = find(messageId);
    if (message == nullptr || message->role != AiMessageRole::assistant)
    {
        return false;
    }
    message->metrics = metrics;
    message->estimatedCostUsd = costEstimate.usd;
    message->costCatalogDate =
        QString::fromLatin1(costEstimate.catalogDate.data(), static_cast<qsizetype>(costEstimate.catalogDate.size()));
    message->longContextRates = costEstimate.longContextRatesApplied;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row),
                     {WallTimeMillisecondsRole, FirstTokenMillisecondsRole, RetryCountRole, EstimatedCostKnownRole,
                      EstimatedCostUsdRole, CostCatalogDateRole, LongContextRatesRole});
    return true;
}

bool AiConversationModel::failAssistantMessage(const std::uint64_t messageId, QString error)
{
    auto *message = find(messageId);
    if (message == nullptr || message->state != MessageState::streaming)
    {
        return false;
    }
    bool truncated = false;
    message->error = boundedUtf8(std::move(error), std::size_t{4} * 1024, truncated);
    message->state = MessageState::failed;
    message->truncated = message->truncated || truncated;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {StateRole, ErrorRole, TruncatedRole});
    updateStreaming();
    return true;
}

bool AiConversationModel::cancelAssistantMessage(const std::uint64_t messageId)
{
    auto *message = find(messageId);
    if (message == nullptr || message->state != MessageState::streaming)
    {
        return false;
    }
    message->error.clear();
    message->state = MessageState::cancelled;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {StateRole, ErrorRole});
    updateStreaming();
    return true;
}

void AiConversationModel::clear()
{
    if (m_messages.empty() && m_evidenceMessages.empty())
    {
        return;
    }
    beginResetModel();
    m_messages.clear();
    m_evidenceMessages.clear();
    m_totalBytes = 0;
    m_evidenceBytes = 0;
    endResetModel();
    emit countChanged();
    updateStreaming();
}

QString AiConversationModel::roleToken(const AiMessageRole role)
{
    switch (role)
    {
        case AiMessageRole::system:
            return QStringLiteral("system");
        case AiMessageRole::user:
            return QStringLiteral("user");
        case AiMessageRole::assistant:
            return QStringLiteral("assistant");
        case AiMessageRole::tool:
            return QStringLiteral("tool");
    }
    return QStringLiteral("system");
}

QString AiConversationModel::stateToken(const MessageState state)
{
    switch (state)
    {
        case MessageState::complete:
            return QStringLiteral("complete");
        case MessageState::streaming:
            return QStringLiteral("streaming");
        case MessageState::failed:
            return QStringLiteral("failed");
        case MessageState::cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

QString AiConversationModel::boundedUtf8(QString text, const std::size_t maximumBytes, bool &truncated)
{
    auto utf8 = text.toUtf8();
    if (std::cmp_less_equal(utf8.size(), maximumBytes))
    {
        truncated = false;
        return text;
    }
    auto count = std::min<std::size_t>(maximumBytes, static_cast<std::size_t>(utf8.size()));
    while (count > 0 && (static_cast<unsigned char>(utf8.at(static_cast<qsizetype>(count))) & 0xC0U) == 0x80U)
    {
        --count;
    }
    utf8.truncate(static_cast<qsizetype>(count));
    truncated = true;
    return QString::fromUtf8(utf8);
}

QString AiConversationModel::commandSuggestion(const QString &text)
{
    constexpr qsizetype maximumCommandCharacters = qsizetype{16} * 1024;
    qsizetype searchFrom = 0;
    QString candidate;
    int blocks = 0;
    while (true)
    {
        const qsizetype opening = text.indexOf(QStringLiteral("```"), searchFrom);
        if (opening < 0)
        {
            break;
        }
        const qsizetype headerEnd = text.indexOf(QLatin1Char('\n'), opening + 3);
        if (headerEnd < 0)
        {
            return {};
        }
        const QString language = text.mid(opening + 3, headerEnd - opening - 3).trimmed().toLower();
        const qsizetype closing = text.indexOf(QStringLiteral("```"), headerEnd + 1);
        if (closing < 0)
        {
            return {};
        }
        static const QStringList supportedLanguages = {QString{},
                                                       QStringLiteral("sh"),
                                                       QStringLiteral("shell"),
                                                       QStringLiteral("bash"),
                                                       QStringLiteral("zsh"),
                                                       QStringLiteral("fish"),
                                                       QStringLiteral("pwsh"),
                                                       QStringLiteral("powershell"),
                                                       QStringLiteral("cmd"),
                                                       QStringLiteral("bat"),
                                                       QStringLiteral("batch")};
        if (supportedLanguages.contains(language))
        {
            ++blocks;
            candidate = text.mid(headerEnd + 1, closing - headerEnd - 1);
        }
        searchFrom = closing + 3;
    }
    candidate = candidate.trimmed();
    if (blocks != 1 || candidate.isEmpty() || candidate.size() > maximumCommandCharacters
        || candidate.contains(QChar::Null))
    {
        return {};
    }
    return candidate;
}

AiConversationModel::Message *AiConversationModel::find(const std::uint64_t messageId)
{
    const auto iterator = std::ranges::find(m_messages, messageId, &Message::id);
    return iterator == m_messages.end() ? nullptr : &*iterator;
}

int AiConversationModel::indexOf(const std::uint64_t messageId) const
{
    const auto iterator = std::ranges::find(m_messages, messageId, &Message::id);
    return iterator == m_messages.end() ? -1 : static_cast<int>(std::distance(m_messages.begin(), iterator));
}

bool AiConversationModel::discardOldestProviderReplay(const std::optional<std::uint64_t> excludedMessageId)
{
    const auto iterator = std::ranges::find_if(m_messages, [excludedMessageId](const Message &message) {
        return !message.providerReplayJson.empty()
               && (!excludedMessageId.has_value() || message.id != *excludedMessageId);
    });
    if (iterator == m_messages.end())
    {
        return false;
    }
    const std::size_t bytes = iterator->providerReplayJson.size();
    iterator->providerReplayJson.clear();
    iterator->bytes -= std::min(iterator->bytes, bytes);
    iterator->truncated = true;
    m_totalBytes -= std::min(m_totalBytes, bytes);
    const auto row = static_cast<int>(std::distance(m_messages.begin(), iterator));
    emit dataChanged(index(row), index(row), {TruncatedRole});
    return true;
}

void AiConversationModel::evictFor(const std::size_t incomingBytes)
{
    while (m_totalBytes + incomingBytes > m_limits.maxConversationBytes && discardOldestProviderReplay())
    {
    }
    while (
        !m_messages.empty()
        && (m_messages.size() >= m_limits.maxMessages || m_totalBytes + incomingBytes > m_limits.maxConversationBytes))
    {
        const std::uint64_t evictedMessageId = m_messages.front().id;
        std::erase_if(m_evidenceMessages, [this, evictedMessageId](const EvidenceMessage &evidence) {
            if (evidence.afterMessageId != evictedMessageId)
            {
                return false;
            }
            m_evidenceBytes -= std::min(m_evidenceBytes, evidence.bytes);
            return true;
        });
        beginRemoveRows({}, 0, 0);
        m_totalBytes -= std::min(m_totalBytes, m_messages.front().bytes);
        m_messages.erase(m_messages.begin());
        endRemoveRows();
        emit countChanged();
    }
}

void AiConversationModel::updateStreaming()
{
    const bool value = std::ranges::any_of(m_messages, [](const Message &message) {
        return message.state == MessageState::streaming;
    });
    if (m_streaming == value)
    {
        return;
    }
    m_streaming = value;
    emit streamingChanged();
}

} // namespace ztermy::ai
