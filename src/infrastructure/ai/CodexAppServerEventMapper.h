#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/ai/CodexAppServerProtocol.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>

#include <expected>
#include <optional>
#include <vector>

namespace ztermy::ai
{

class CodexAppServerEventMapper final
{
public:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, QString> map(const CodexAppServerMessage &message);
    void reset() noexcept;

private:
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, QString>
    mapNotification(const CodexAppServerMessage &message);
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, QString> mapItemStarted(const QJsonObject &params);
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, QString> mapItemCompleted(const QJsonObject &params);
    [[nodiscard]] std::expected<std::vector<AiStreamEvent>, QString> mapTurnCompleted(const QJsonObject &params);

    QSet<QString> m_textItemsWithDeltas;
    QSet<QString> m_reasoningItemsWithSummary;
    QHash<QString, QByteArray> m_bufferedRawReasoning;
    std::optional<AiProviderError> m_pendingError;
    QString m_turnId;
};

} // namespace ztermy::ai
