#include "application/ai/AiPrivacyDiagnostics.h"

#include <QtTest/QTest>

#include <QJsonDocument>

namespace
{
class AiPrivacyDiagnosticsTests final : public QObject
{
    Q_OBJECT

private slots:
    void classifiesEndpointWithoutExportingItsHost();
    void reportsOnlyBoundedCountsAndPolicyFlags();
};

void AiPrivacyDiagnosticsTests::classifiesEndpointWithoutExportingItsHost()
{
    QCOMPARE(ztermy::ai::aiEndpointScope(QUrl(QStringLiteral("http://127.0.0.1:11434"))), QStringLiteral("loopback"));
    QCOMPARE(ztermy::ai::aiEndpointScope(QUrl(QStringLiteral("https://192.168.1.25/v1"))),
             QStringLiteral("private-network"));
    QCOMPARE(ztermy::ai::aiEndpointScope(QUrl(QStringLiteral("https://api.example.invalid/v1"))),
             QStringLiteral("remote"));
    QCOMPARE(ztermy::ai::aiEndpointScope(QUrl{}), QStringLiteral("invalid"));

    const auto report =
        ztermy::ai::buildAiPrivacyDiagnostics({.providerKind = QStringLiteral("openai"),
                                               .endpoint = QUrl(QStringLiteral("https://secret-provider.example/v1")),
                                               .permissionMode = QStringLiteral("observer"),
                                               .modelConfigured = true,
                                               .apiKeyConfigured = true});
    const QByteArray serialized = QJsonDocument(report).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("secret-provider"));
    QVERIFY(!serialized.contains("api-key"));
    QCOMPARE(report.value(QStringLiteral("provider")).toObject().value(QStringLiteral("endpointScope")).toString(),
             QStringLiteral("remote"));
}

void AiPrivacyDiagnosticsTests::reportsOnlyBoundedCountsAndPolicyFlags()
{
    const auto report =
        ztermy::ai::buildAiPrivacyDiagnostics({.providerKind = QStringLiteral("ollama"),
                                               .endpoint = QUrl(QStringLiteral("http://localhost:11434")),
                                               .permissionMode = QStringLiteral("ask-each-write"),
                                               .modelConfigured = true,
                                               .automaticContext = true,
                                               .encryptedHistoryEnabled = true,
                                               .activeConversations = 2,
                                               .activeRequests = 1,
                                               .contextItems = 7,
                                               .redactedContextItems = 3,
                                               .truncatedContextItems = 1,
                                               .serializedContextBytes = 4096,
                                               .mcpServers = 2,
                                               .readyMcpServers = 1,
                                               .approvedMcpTools = 4});
    QCOMPARE(report.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("activeConversations")).toInt(),
             2);
    QCOMPARE(report.value(QStringLiteral("context")).toObject().value(QStringLiteral("redactedItemCount")).toInt(), 3);
    QCOMPARE(report.value(QStringLiteral("mcp")).toObject().value(QStringLiteral("approvedToolCount")).toInt(), 4);
    const QJsonObject boundary = report.value(QStringLiteral("providerRequestBoundary")).toObject();
    QVERIFY(boundary.value(QStringLiteral("includesUserPrompt")).toBool());
    QVERIFY(!boundary.value(QStringLiteral("includesRawTerminalInput")).toBool());
    QVERIFY(!boundary.value(QStringLiteral("includesCredentialInPrompt")).toBool());
}
} // namespace

QTEST_GUILESS_MAIN(AiPrivacyDiagnosticsTests)

#include "ai_privacy_diagnostics_tests.moc"
