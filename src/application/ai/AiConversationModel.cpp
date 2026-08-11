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
        default:
            return {};
    }
}

QHash<int, QByteArray> AiConversationModel::roleNames() const
{
    return {{MessageIdRole, "messageId"},
            {MessageRole, "messageRole"},
            {TextRole, "text"},
            {StateRole, "state"},
            {ErrorRole, "error"},
            {TruncatedRole, "truncated"},
            {InputTokensRole, "inputTokens"},
            {OutputTokensRole, "outputTokens"}};
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
        if (message.state == MessageState::streaming || message.state == MessageState::failed)
        {
            continue;
        }
        messages.push_back(AiChatMessage{.role = message.role, .content = message.text.toUtf8().toStdString()});
    }
    return messages;
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

bool AiConversationModel::completeAssistantMessage(const std::uint64_t messageId, std::optional<AiTokenUsage> usage)
{
    auto *message = find(messageId);
    if (message == nullptr || message->state != MessageState::streaming)
    {
        return false;
    }
    message->state = MessageState::complete;
    message->usage = usage;
    const auto row = indexOf(messageId);
    emit dataChanged(index(row), index(row), {StateRole, InputTokensRole, OutputTokensRole});
    updateStreaming();
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

void AiConversationModel::clear()
{
    if (m_messages.empty())
    {
        return;
    }
    beginResetModel();
    m_messages.clear();
    m_totalBytes = 0;
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
