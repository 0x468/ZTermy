#include "infrastructure/ai/AiPermissionRuleStore.h"

#include <QtTest/QTest>

#include <QFile>
#include <QTemporaryDir>

namespace
{
using namespace ztermy::ai;

class AiPermissionRuleStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsProfileAndGlobalRules();
    void refusesToPersistEphemeralRules();
    void recoversMalformedPrimaryFromBackup();
    void rejectsInvalidRegexAndDuplicateIds();
};

void AiPermissionRuleStoreTests::roundTripsProfileAndGlobalRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AiPermissionRuleStore store(directory.filePath(QStringLiteral("ai-permission-rules.json")));
    const std::vector rules{AiPermissionRule{.id = "profile-docker",
                                             .capability = AiPermissionCapability::terminalCommand,
                                             .matcher = AiPermissionRuleMatcher::prefix,
                                             .pattern = "docker ",
                                             .disposition = AiPermissionDisposition::allow,
                                             .duration = AiPermissionRuleDuration::profile,
                                             .profileId = "docker-host"},
                            AiPermissionRule{.id = "global-reboot",
                                             .capability = AiPermissionCapability::terminalCommand,
                                             .matcher = AiPermissionRuleMatcher::regex,
                                             .pattern = R"(.*reboot.*)",
                                             .disposition = AiPermissionDisposition::deny,
                                             .duration = AiPermissionRuleDuration::global}};
    QVERIFY(store.save(rules).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, rules);
}

void AiPermissionRuleStoreTests::refusesToPersistEphemeralRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AiPermissionRuleStore store(directory.filePath(QStringLiteral("ai-permission-rules.json")));
    const AiPermissionRule rule{.id = "session",
                                .matcher = AiPermissionRuleMatcher::all,
                                .duration = AiPermissionRuleDuration::session,
                                .sessionId = "terminal-1"};
    QVERIFY(!store.save({rule}).has_value());
}

void AiPermissionRuleStoreTests::recoversMalformedPrimaryFromBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ai-permission-rules.json"));
    AiPermissionRuleStore store(path);
    const AiPermissionRule first{.id = "first",
                                 .matcher = AiPermissionRuleMatcher::all,
                                 .duration = AiPermissionRuleDuration::global};
    auto second = first;
    second.id = "second";
    QVERIFY(store.save({first}).has_value());
    QVERIFY(store.save({second}).has_value());
    QFile primary(path);
    QVERIFY(primary.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(primary.write("{malformed"), qint64{10});
    primary.close();
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(store.lastLoadRecoveredFromBackup());
    QCOMPARE(loaded->front().id, std::string("first"));
}

void AiPermissionRuleStoreTests::rejectsInvalidRegexAndDuplicateIds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ai-permission-rules.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schema_version":1,"rules":[{"id":"same","capability":"terminal-command","matcher":"regex","pattern":"(","decision":"allow","duration":"global"},{"id":"same","capability":"terminal-command","matcher":"all","pattern":"","decision":"deny","duration":"global"}]})");
    file.close();
    AiPermissionRuleStore store(path);
    QVERIFY(!store.load().has_value());
}
} // namespace

QTEST_GUILESS_MAIN(AiPermissionRuleStoreTests)

#include "ai_permission_rule_store_tests.moc"
