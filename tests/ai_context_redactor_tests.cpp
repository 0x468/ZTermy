#include "domain/ai/AiContextRedactor.h"

#include <QTest>

#include <string>

namespace
{

using ztermy::ai::AiContextRedactor;
using ztermy::ai::AiRedactionRuleType;
using ztermy::ai::AiSecretKind;
using ztermy::ai::AiUserRedactionRule;

[[nodiscard]] constexpr std::size_t index(const AiSecretKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

class AiContextRedactorTests final : public QObject
{
    Q_OBJECT

private slots:
    void redactsBuiltInSecretClasses();
    void appliesUserLiteralAndRegexRules();
    void reportsInvalidRulesWithoutDisablingBuiltIns();
};

void AiContextRedactorTests::redactsBuiltInSecretClasses()
{
    const std::string input =
        "Authorization: Bearer header-secret-12345\n"
        "url=https://alice:password@example.test/path\n"
        "OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz123456\n"
        "DB_PASSWORD='database-secret'\n"
        "-----BEGIN OPENSSH PRIVATE KEY-----\nprivate-material\n-----END OPENSSH PRIVATE KEY-----\n";
    const auto result = AiContextRedactor{}.redact(input);
    QVERIFY(!result.text.contains("header-secret"));
    QVERIFY(!result.text.contains("alice:password"));
    QVERIFY(!result.text.contains("sk-abcdefghijklmnopqrstuvwxyz"));
    QVERIFY(!result.text.contains("database-secret"));
    QVERIFY(!result.text.contains("private-material"));
    QVERIFY(result.text.contains("[REDACTED:AUTHORIZATION]"));
    QVERIFY(result.text.contains("https://[REDACTED:URI_CREDENTIAL]@example.test"));
    QVERIFY(result.text.contains("[REDACTED:API_KEY]"));
    QVERIFY(result.text.contains("DB_PASSWORD=[REDACTED:SHELL_SECRET]"));
    QVERIFY(result.text.contains("[REDACTED:PRIVATE_KEY]"));
    QCOMPARE(result.counts[index(AiSecretKind::privateKey)], std::size_t{1});
    QCOMPARE(result.counts[index(AiSecretKind::authorizationHeader)], std::size_t{1});
    QCOMPARE(result.counts[index(AiSecretKind::uriCredential)], std::size_t{1});
    QVERIFY(result.totalRedactions() >= std::size_t{5});
}

void AiContextRedactorTests::appliesUserLiteralAndRegexRules()
{
    const std::vector rules{AiUserRedactionRule{.id = "literal",
                                                .type = AiRedactionRuleType::literal,
                                                .pattern = "Project Falcon",
                                                .caseSensitive = false},
                            AiUserRedactionRule{.id = "ticket",
                                                .type = AiRedactionRuleType::regularExpression,
                                                .pattern = R"(INC-[0-9]{4})"}};
    const auto result = AiContextRedactor{}.redact("project falcon INC-2048", rules);
    QCOMPARE(result.text, std::string("[REDACTED:USER_RULE] [REDACTED:USER_RULE]"));
    QCOMPARE(result.counts[index(AiSecretKind::userRule)], std::size_t{2});
    QVERIFY(result.invalidRuleIds.empty());
}

void AiContextRedactorTests::reportsInvalidRulesWithoutDisablingBuiltIns()
{
    const std::vector rules{
        AiUserRedactionRule{.id = "empty", .pattern = ""},
        AiUserRedactionRule{.id = "broken", .type = AiRedactionRuleType::regularExpression, .pattern = "(["}};
    const auto result = AiContextRedactor{}.redact("token sk-abcdefghijklmnopqrstuvwxyz123456", rules);
    QCOMPARE(result.invalidRuleIds.size(), std::size_t{2});
    QVERIFY(result.text.contains("[REDACTED:API_KEY]"));
}

} // namespace

QTEST_GUILESS_MAIN(AiContextRedactorTests)

#include "ai_context_redactor_tests.moc"
