#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiUsageReporting.h"

#include <QAbstractListModel>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct AiConversationLimits final
{
    std::size_t maxMessages = 64;
    std::size_t maxMessageBytes = std::size_t{256} * 1024;
    std::size_t maxConversationBytes = std::size_t{20} * 1024 * 1024;
};

enum class AiConversationTranscriptRole : std::uint8_t
{
    user,
    assistant,
    evidence,
};

struct AiConversationTranscriptEntry final
{
    AiConversationTranscriptRole role = AiConversationTranscriptRole::user;
    std::string content;
    std::string reasoning;
    std::vector<AiToolActivity> toolActivities;
    std::vector<AiWebSource> sources;
    std::string providerReplayJson;
    std::optional<AiTokenUsage> usage;
    std::optional<AiTurnMetrics> metrics;
    std::optional<double> estimatedCostUsd;
    std::string costCatalogDate;
    bool longContextRates = false;
    bool truncated = false;
    std::vector<AiContextAttachmentSummary> contextAttachments;

    [[nodiscard]] friend bool operator==(const AiConversationTranscriptEntry &,
                                         const AiConversationTranscriptEntry &) = default;
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
        ReasoningRole,
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
        CommandSuggestionRole,
        HasCommandSuggestionRole,
        ToolActivitiesRole,
        ImageAttachmentsRole,
        ContextAttachmentsRole,
        SourcesRole,
        ToolEvidenceStateRole,
        ToolEvidenceFailedCountRole,
        ToolEvidencePendingCountRole,
        ToolEvidenceFailedSideEffectCountRole,
    };

    explicit AiConversationModel(AiConversationLimits limits = {}, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool streaming() const noexcept;
    [[nodiscard]] const AiConversationLimits &limits() const noexcept;
    [[nodiscard]] std::vector<AiChatMessage> providerMessages() const;
    [[nodiscard]] std::vector<AiChatMessage> providerMessagesWithEvidence(bool retryContinuation = false) const;
    [[nodiscard]] std::vector<AiConversationTranscriptEntry> transcript() const;
    [[nodiscard]] bool restoreTranscript(const std::vector<AiConversationTranscriptEntry> &entries);
    [[nodiscard]] bool appendEvidenceMessage(QString text);
    [[nodiscard]] bool appendEvidenceMessageAfter(std::uint64_t messageId, QString text);
    [[nodiscard]] std::uint64_t appendUserMessage(QString text, std::vector<AiImageAttachment> images = {},
                                                  std::vector<AiContextAttachmentSummary> contextAttachments = {});
    [[nodiscard]] std::uint64_t beginAssistantMessage();
    [[nodiscard]] bool appendAssistantDelta(std::uint64_t messageId, QString delta);
    [[nodiscard]] bool appendAssistantReasoningDelta(std::uint64_t messageId, QString delta);
    [[nodiscard]] bool appendAssistantSource(std::uint64_t messageId, AiWebSource source);
    [[nodiscard]] bool upsertAssistantToolActivity(std::uint64_t messageId, QString toolCallId, QString toolName,
                                                   QString summary, QString state, QString resultCode,
                                                   bool sideEffecting, bool highRisk);
    // A null detail preserves the existing value; a non-null empty string clears it.
    [[nodiscard]] bool setAssistantToolDetails(std::uint64_t messageId, const QString &toolCallId,
                                               QString argumentsJson, QString resultJson);
    [[nodiscard]] bool setAssistantProviderReplay(std::uint64_t messageId, std::span<const AiToolExchange> toolHistory,
                                                  std::string_view finalAssistantContentJson);
    [[nodiscard]] bool completeAssistantMessage(std::uint64_t messageId,
                                                std::optional<AiTokenUsage> usage = std::nullopt);
    // Late usage updates (some providers emit usage after the completion
    // event) still land on the finished message so the token display and the
    // cost estimate are never zero.
    [[nodiscard]] bool updateAssistantUsage(std::uint64_t messageId, const AiTokenUsage &usage);
    [[nodiscard]] bool setAssistantMetrics(std::uint64_t messageId, const AiTurnMetrics &metrics,
                                           const AiCostEstimate &costEstimate = {});
    [[nodiscard]] bool failAssistantMessage(std::uint64_t messageId, QString error);
    [[nodiscard]] bool cancelAssistantMessage(std::uint64_t messageId);
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
        cancelled,
    };

    struct Message final
    {
        std::uint64_t id = 0;
        AiMessageRole role = AiMessageRole::user;
        QString text;
        QString reasoning;
        QString error;
        MessageState state = MessageState::complete;
        std::optional<AiTokenUsage> usage;
        std::optional<AiTurnMetrics> metrics;
        std::optional<double> estimatedCostUsd;
        QString costCatalogDate;
        std::size_t bytes = 0;
        bool truncated = false;
        bool longContextRates = false;
        QString commandSuggestion;
        std::vector<AiToolActivity> toolActivities;
        std::vector<AiImageAttachment> images;
        std::vector<AiWebSource> sources;
        std::string providerReplayJson;
        std::vector<AiContextAttachmentSummary> contextAttachments;
    };

    struct EvidenceMessage final
    {
        std::uint64_t afterMessageId = 0;
        QString text;
        std::size_t bytes = 0;
    };

    [[nodiscard]] static QString roleToken(AiMessageRole role);
    [[nodiscard]] static QString stateToken(MessageState state);
    [[nodiscard]] static QString boundedUtf8(QString text, std::size_t maximumBytes, bool &truncated);
    [[nodiscard]] static QString commandSuggestion(const QString &text);
    [[nodiscard]] Message *find(std::uint64_t messageId);
    [[nodiscard]] int indexOf(std::uint64_t messageId) const;
    [[nodiscard]] bool discardOldestProviderReplay(std::optional<std::uint64_t> excludedMessageId = std::nullopt);
    void evictFor(std::size_t incomingBytes);
    void updateStreaming();

    AiConversationLimits m_limits;
    std::vector<Message> m_messages;
    std::vector<EvidenceMessage> m_evidenceMessages;
    std::size_t m_totalBytes = 0;
    std::size_t m_evidenceBytes = 0;
    std::uint64_t m_nextMessageId = 1;
    bool m_streaming = false;
};

} // namespace ztermy::ai
