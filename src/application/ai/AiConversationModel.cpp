#include "application/ai/AiConversationModel.h"

#include <QByteArray>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <utility>

namespace ztermy::ai
{

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
            return message.toolActivities;
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
            {ToolActivitiesRole, "toolActivities"}};
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
    for (const auto &message : m_messages)
    {
        if (message.state == MessageState::streaming || message.state == MessageState::failed
            || message.state == MessageState::cancelled)
        {
            continue;
        }
        messages.push_back(AiChatMessage{.role = message.role, .content = message.text.toUtf8().toStdString()});
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
                               .content = message.text.toUtf8().toStdString()});
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
    const auto entries = transcript();
    messages.reserve(entries.size());
    for (const auto &entry : entries)
    {
        messages.push_back({.role = entry.role == AiConversationTranscriptRole::assistant ? AiMessageRole::assistant
                                                                                          : AiMessageRole::user,
                            .content = entry.content});
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
        if ((entry.content.empty() && entry.role != AiConversationTranscriptRole::assistant)
            || entry.content.size() > m_limits.maxMessageBytes)
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
            messageBytes += entry.content.size();
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
        if ((!entry.content.empty() && !appendAssistantDelta(messageId, QString::fromUtf8(entry.content)))
            || !completeAssistantMessage(messageId))
        {
            clear();
            return false;
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
    m_evidenceMessages.push_back(
        {.afterMessageId = m_messages.back().id, .text = std::move(text), .bytes = bytes});
    return true;
}

std::uint64_t AiConversationModel::appendUserMessage(QString text)
{
    bool truncated = false;
    text = boundedUtf8(std::move(text), m_limits.maxMessageBytes, truncated);
    const auto bytes = static_cast<std::size_t>(text.toUtf8().size());
    evictFor(bytes);
    const auto row = rowCount();
    beginInsertRows({}, row, row);
    const auto id = m_nextMessageId++;
    m_messages.push_back(Message{.id = id,
                                 .role = AiMessageRole::user,
                                 .text = std::move(text),
                                 .bytes = bytes,
                                 .truncated = truncated});
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
    QVariantMap activity{{QStringLiteral("id"), toolCallId},         {QStringLiteral("name"), toolName},
                         {QStringLiteral("summary"), summary},       {QStringLiteral("state"), state},
                         {QStringLiteral("resultCode"), resultCode}, {QStringLiteral("sideEffecting"), sideEffecting},
                         {QStringLiteral("highRisk"), highRisk}};
    qsizetype existingIndex = -1;
    for (qsizetype index = 0; index < message->toolActivities.size(); ++index)
    {
        if (message->toolActivities.at(index).toMap().value(QStringLiteral("id")).toString() == toolCallId)
        {
            existingIndex = index;
            break;
        }
    }
    if (existingIndex >= 0)
    {
        message->toolActivities[existingIndex] = activity;
    }
    else
    {
        constexpr qsizetype maximumToolActivities = 32;
        if (message->toolActivities.size() >= maximumToolActivities)
        {
            message->toolActivities.removeFirst();
        }
        message->toolActivities.push_back(activity);
    }
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {ToolActivitiesRole});
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
    emit dataChanged(index(row), index(row),
                     {InputTokensRole, OutputTokensRole, CachedInputTokensRole, ReasoningTokensRole,
                      UsageAvailableRole});
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

void AiConversationModel::evictFor(const std::size_t incomingBytes)
{
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
