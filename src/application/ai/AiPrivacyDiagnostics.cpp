#include "application/ai/AiPrivacyDiagnostics.h"

#include <QJsonArray>

namespace ztermy::ai
{
namespace
{
[[nodiscard]] bool isPrivateIpv4(const QString &host)
{
    const QStringList parts = host.split(QLatin1Char('.'));
    if (parts.size() != 4)
    {
        return false;
    }
    bool ok = false;
    const int first = parts.at(0).toInt(&ok);
    if (!ok || first < 0 || first > 255)
    {
        return false;
    }
    const int second = parts.at(1).toInt(&ok);
    if (!ok || second < 0 || second > 255)
    {
        return false;
    }
    for (qsizetype index = 2; index < parts.size(); ++index)
    {
        const int value = parts.at(index).toInt(&ok);
        if (!ok || value < 0 || value > 255)
        {
            return false;
        }
    }
    return first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168)
           || (first == 169 && second == 254);
}

[[nodiscard]] qint64 count(const std::size_t value)
{
    return static_cast<qint64>(value);
}
} // namespace

QString aiEndpointScope(const QUrl &endpoint)
{
    if (!endpoint.isValid() || endpoint.host().isEmpty())
    {
        return QStringLiteral("invalid");
    }
    const QString host = endpoint.host().toLower();
    if (host == QStringLiteral("localhost") || host == QStringLiteral("::1") || host.startsWith(QStringLiteral("127.")))
    {
        return QStringLiteral("loopback");
    }
    if (isPrivateIpv4(host) || host.startsWith(QStringLiteral("fc")) || host.startsWith(QStringLiteral("fd"))
        || host.startsWith(QStringLiteral("fe8")) || host.startsWith(QStringLiteral("fe9"))
        || host.startsWith(QStringLiteral("fea")) || host.startsWith(QStringLiteral("feb")))
    {
        return QStringLiteral("private-network");
    }
    return QStringLiteral("remote");
}

QJsonObject buildAiPrivacyDiagnostics(const AiPrivacySnapshot &snapshot)
{
    const QJsonObject provider{{QStringLiteral("kind"), snapshot.providerKind},
                               {QStringLiteral("endpointScope"), aiEndpointScope(snapshot.endpoint)},
                               {QStringLiteral("modelConfigured"), snapshot.modelConfigured},
                               {QStringLiteral("apiCredentialConfigured"), snapshot.apiKeyConfigured}};
    const QJsonObject policy{{QStringLiteral("permissionMode"), snapshot.permissionMode},
                             {QStringLiteral("automaticContext"), snapshot.automaticContext},
                             {QStringLiteral("encryptedHistoryEnabled"), snapshot.encryptedHistoryEnabled}};
    const QJsonObject runtime{{QStringLiteral("activeConversations"), count(snapshot.activeConversations)},
                              {QStringLiteral("activeRequests"), count(snapshot.activeRequests)}};
    const QJsonObject context{{QStringLiteral("itemCount"), count(snapshot.contextItems)},
                              {QStringLiteral("redactedItemCount"), count(snapshot.redactedContextItems)},
                              {QStringLiteral("truncatedItemCount"), count(snapshot.truncatedContextItems)},
                              {QStringLiteral("serializedBytes"), count(snapshot.serializedContextBytes)}};
    const QJsonObject mcp{{QStringLiteral("serverCount"), count(snapshot.mcpServers)},
                          {QStringLiteral("readyServerCount"), count(snapshot.readyMcpServers)},
                          {QStringLiteral("approvedToolCount"), count(snapshot.approvedMcpTools)}};
    const QJsonObject providerRequest{
        {QStringLiteral("includesUserPrompt"), true},
        {QStringLiteral("includesConversationMessages"), true},
        {QStringLiteral("includesOnlyPreviewedBoundedTerminalEvidence"), true},
        {QStringLiteral("includesRawTerminalInput"), false},
        {QStringLiteral("includesCredentialInPrompt"), false},
        {QStringLiteral("includesPrivateKeyMaterial"), false},
        {QStringLiteral("includesArbitraryUnselectedFiles"), false},
    };
    const QJsonObject diagnosticExport{
        {QStringLiteral("includesPromptsOrResponses"), false}, {QStringLiteral("includesTerminalContent"), false},
        {QStringLiteral("includesCredentials"), false},        {QStringLiteral("includesEndpointHost"), false},
        {QStringLiteral("includesModelIdentifier"), false},    {QStringLiteral("includesMcpArgumentsOrResults"), false},
    };
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("provider"), provider},
            {QStringLiteral("policy"), policy},
            {QStringLiteral("runtime"), runtime},
            {QStringLiteral("context"), context},
            {QStringLiteral("mcp"), mcp},
            {QStringLiteral("providerRequestBoundary"), providerRequest},
            {QStringLiteral("diagnosticExportBoundary"), diagnosticExport}};
}

} // namespace ztermy::ai
