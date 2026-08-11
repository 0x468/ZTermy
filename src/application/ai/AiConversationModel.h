#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiUsageReporting.h"

#include <QAbstractListModel>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ztermy::ai
{

struct AiConversationLimits final
{
    std::size_t maxMessages = 64;
    std::size_t maxMessageBytes = std::size_t{256} * 1024;
    std::size_t maxConversationBytes = std::size_t{1024} * 1024;
};

class AiConversationModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role : int // NOLINT(performance-enum-size) Qt item model roles are int.
    {
        MessageIdRole = Qt::UserRole + 1,
        MessageRole,
        TextRole,
        StateRole,
        ErrorRole,
        TruncatedRole,
        InputTokensRole,
        OutputTokensRole,
        CachedInputTokensRole,
        ReasoningTokensRole,
        UsageAvailableRole,
        WallTimeMillisecondsRole,
        FirstTokenMillisecondsRole,
        RetryCountRole,
        EstimatedCostKnownRole,
        EstimatedCostUsdRole,
        CostCatalogDateRole,
        LongContextRatesRole,
    };

    explicit AiConversationModel(AiConversationLimits limits = {}, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool streaming() const noexcept;
    [[nodiscard]] const AiConversationLimits &limits() const noexcept;
    [[nodiscard]] std::vector<AiChatMessage> providerMessages() const;
    [[nodiscard]] std::uint64_t appendUserMessage(QString text);
    [[nodiscard]] std::uint64_t beginAssistantMessage();
    [[nodiscard]] bool appendAssistantDelta(std::uint64_t messageId, QString delta);
    [[nodiscard]] bool completeAssistantMessage(std::uint64_t messageId,
                                                std::optional<AiTokenUsage> usage = std::nullopt);
    [[nodiscard]] bool setAssistantMetrics(std::uint64_t messageId, const AiTurnMetrics &metrics,
                                           const AiCostEstimate &costEstimate = {});
    [[nodiscard]] bool failAssistantMessage(std::uint64_t messageId, QString error);
    Q_INVOKABLE void clear();

signals:
    void streamingChanged();
    void countChanged();

private:
    enum class MessageState : std::uint8_t
    {
        complete,
        streaming,
        failed,
    };

    struct Message final
    {
        std::uint64_t id = 0;
        AiMessageRole role = AiMessageRole::user;
        QString text;
        QString error;
        MessageState state = MessageState::complete;
        std::optional<AiTokenUsage> usage;
        std::optional<AiTurnMetrics> metrics;
        std::optional<double> estimatedCostUsd;
        QString costCatalogDate;
        std::size_t bytes = 0;
        bool truncated = false;
        bool longContextRates = false;
    };

    [[nodiscard]] static QString roleToken(AiMessageRole role);
    [[nodiscard]] static QString stateToken(MessageState state);
    [[nodiscard]] static QString boundedUtf8(QString text, std::size_t maximumBytes, bool &truncated);
    [[nodiscard]] Message *find(std::uint64_t messageId);
    [[nodiscard]] int indexOf(std::uint64_t messageId) const;
    void evictFor(std::size_t incomingBytes);
    void updateStreaming();

    AiConversationLimits m_limits;
    std::vector<Message> m_messages;
    std::size_t m_totalBytes = 0;
    std::uint64_t m_nextMessageId = 1;
    bool m_streaming = false;
};

} // namespace ztermy::ai
