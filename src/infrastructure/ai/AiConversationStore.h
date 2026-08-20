#pragma once

#include "core/security/CredentialVault.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiUsageReporting.h"

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace ztermy::ai
{

struct AiStoredMessage final
{
    QString role;
    QString text;
    QString reasoning;
    std::vector<AiToolActivity> toolActivities;
    std::vector<AiWebSource> sources;
    std::string providerReplayJson;
    std::optional<AiTokenUsage> usage;
    std::optional<AiTurnMetrics> metrics;
    std::optional<double> estimatedCostUsd;
    QString costCatalogDate;
    bool longContextRates = false;
    bool truncated = false;
    std::vector<AiContextAttachmentSummary> contextAttachments;

    [[nodiscard]] friend bool operator==(const AiStoredMessage &, const AiStoredMessage &) = default;
};

struct AiStoredConversation final
{
    QString id;
    QString title;
    QDateTime updatedAtUtc;
    std::vector<AiStoredMessage> messages;

    [[nodiscard]] friend bool operator==(const AiStoredConversation &, const AiStoredConversation &) = default;
};

enum class AiConversationStoreError : std::uint8_t
{
    invalidPath,
    invalidData,
    unsupportedVersion,
    unavailable,
    locked,
    keyMissing,
    authenticationFailed,
    cryptoError,
    ioError,
};

struct AiConversationStoreLimits final
{
    std::size_t maximumConversations = 50;
    std::size_t maximumMessagesPerConversation = 200;
    std::size_t maximumMessageBytes = std::size_t{512} * 1024;
    std::size_t maximumPlaintextBytes = std::size_t{6} * 1024 * 1024;
    int retentionDays = 90;
};

class AiConversationStore final
{
public:
    AiConversationStore(QString filePath, security::CredentialVault &vault, AiConversationStoreLimits limits = {});

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] const AiConversationStoreLimits &limits() const noexcept;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<std::vector<AiStoredConversation>, AiConversationStoreError> load() const;
    [[nodiscard]] std::expected<void, AiConversationStoreError> upsert(AiStoredConversation conversation) const;
    [[nodiscard]] std::expected<void, AiConversationStoreError> erase(const QString &conversationId) const;
    [[nodiscard]] std::expected<void, AiConversationStoreError> clear() const;
    [[nodiscard]] std::expected<void, AiConversationStoreError> exportDecrypted(const QString &destinationPath) const;

private:
    QString m_filePath;
    security::CredentialVault &m_vault;
    AiConversationStoreLimits m_limits;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::ai
