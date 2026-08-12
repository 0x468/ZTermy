#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <cstddef>

namespace ztermy::ai
{

struct AiPrivacySnapshot final
{
    QString providerKind;
    QUrl endpoint;
    QString permissionMode;
    bool modelConfigured = false;
    bool apiKeyConfigured = false;
    bool automaticContext = false;
    bool encryptedHistoryEnabled = false;
    std::size_t activeConversations = 0;
    std::size_t activeRequests = 0;
    std::size_t contextItems = 0;
    std::size_t redactedContextItems = 0;
    std::size_t truncatedContextItems = 0;
    std::size_t serializedContextBytes = 0;
    std::size_t mcpServers = 0;
    std::size_t readyMcpServers = 0;
    std::size_t approvedMcpTools = 0;
};

[[nodiscard]] QString aiEndpointScope(const QUrl &endpoint);
[[nodiscard]] QJsonObject buildAiPrivacyDiagnostics(const AiPrivacySnapshot &snapshot);

} // namespace ztermy::ai
