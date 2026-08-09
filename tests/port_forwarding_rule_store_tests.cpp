#include "domain/forwarding/PortForwardingRule.h"
#include "infrastructure/forwarding/PortForwardingRuleStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <string>
#include <vector>

namespace
{

using namespace ztermy::forwarding;

[[nodiscard]] PortForwardingRule localRule()
{
    return {
        .id = "rule-local",
        .label = "PostgreSQL",
        .profileId = "profile-main",
        .type = PortForwardingType::Local,
        .bind = {.host = "127.0.0.1", .port = 15432},
        .destination = {.host = "database.internal", .port = 5432},
        .autoStart = true,
    };
}

class PortForwardingRuleStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesTypedRulesAndReferences();
    void savesAndLoadsOrderedNonSecretRules();
    void missingFileLoadsEmptyAndFutureSchemaIsRejected();
    void rejectsMalformedAndDuplicateRules();
    void recoversLastKnownGoodRules();
};

void PortForwardingRuleStoreTests::validatesTypedRulesAndReferences()
{
    const PortForwardingRule local = localRule();
    QVERIFY(validPortForwardingRule(local));

    PortForwardingRule dynamic = local;
    dynamic.id = "rule-dynamic";
    dynamic.type = PortForwardingType::Dynamic;
    dynamic.bind.port = 1080;
    dynamic.destination = {};
    QVERIFY(validPortForwardingRule(dynamic));

    PortForwardingRule remote = local;
    remote.id = "rule-remote";
    remote.type = PortForwardingType::Remote;
    remote.bind = {.host = "127.0.0.1", .port = 18080};
    remote.destination = {.host = "127.0.0.1", .port = 8080};
    QVERIFY(validPortForwardingRule(remote));

    std::vector rules{local, dynamic, remote};
    QVERIFY(validPortForwardingRules(rules));
    QVERIFY(portForwardingRulesReferenceProfile(rules, "profile-main"));
    QCOMPARE(findPortForwardingRule(rules, "rule-dynamic"), rules.cbegin() + 1);

    rules[2].id = rules[0].id;
    QVERIFY(!validPortForwardingRules(rules));
    dynamic.destination = {.host = "stale.example", .port = 22};
    QVERIFY(!validPortForwardingRule(dynamic));
}

void PortForwardingRuleStoreTests::savesAndLoadsOrderedNonSecretRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("port-forwarding.json"));
    PortForwardingRuleStore store(path);

    PortForwardingRule dynamic = localRule();
    dynamic.id = "rule-dynamic";
    dynamic.label = "Browser SOCKS";
    dynamic.type = PortForwardingType::Dynamic;
    dynamic.bind.port = 1080;
    dynamic.destination = {};
    dynamic.autoStart = false;
    const std::vector rules{localRule(), dynamic};

    QVERIFY(store.save(rules).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, rules);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    QVERIFY(payload.contains("\"schemaVersion\": 1"));
    QVERIFY(payload.contains("\"profileId\": \"profile-main\""));
    QVERIFY(!payload.contains("password"));
    QVERIFY(!payload.contains("credential"));
    QVERIFY(!payload.contains("secret"));
}

void PortForwardingRuleStoreTests::missingFileLoadsEmptyAndFutureSchemaIsRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("port-forwarding.json"));
    PortForwardingRuleStore store(path);
    const auto missing = store.load();
    QVERIFY(missing.has_value());
    QVERIFY(missing->empty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray futurePayload = R"({"schemaVersion":2,"rules":[]})";
    QCOMPARE(file.write(futurePayload), futurePayload.size());
    file.close();
    const auto future = store.load();
    QVERIFY(!future.has_value());
    QCOMPARE(future.error(), PortForwardingRuleStoreError::UnsupportedVersion);
}

void PortForwardingRuleStoreTests::rejectsMalformedAndDuplicateRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("port-forwarding.json"));
    PortForwardingRuleStore store(path);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray duplicate = R"({
      "schemaVersion": 1,
      "rules": [
        {"id":"same","label":"One","profileId":"p","type":"dynamic","bind":{"host":"127.0.0.1","port":1080},"autoStart":false},
        {"id":"same","label":"Two","profileId":"p","type":"dynamic","bind":{"host":"127.0.0.1","port":1081},"autoStart":false}
      ]
    })";
    QCOMPARE(file.write(duplicate), duplicate.size());
    file.close();
    const auto loaded = store.load();
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error(), PortForwardingRuleStoreError::InvalidDocument);

    PortForwardingRule invalid = localRule();
    invalid.bind.port = 0;
    const std::vector invalidRules{invalid};
    const auto saved = store.save(invalidRules);
    QVERIFY(!saved.has_value());
    QCOMPARE(saved.error(), PortForwardingRuleStoreError::InvalidDocument);
}

void PortForwardingRuleStoreTests::recoversLastKnownGoodRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("port-forwarding.json"));
    PortForwardingRuleStore store(path);

    const PortForwardingRule first = localRule();
    PortForwardingRule second = first;
    second.label = "Updated PostgreSQL";
    const std::vector firstGeneration{first};
    const std::vector secondGeneration{second};
    QVERIFY(store.save(firstGeneration));
    QVERIFY(store.save(secondGeneration));
    QFile damaged(path);
    QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(damaged.write("{truncated"), qint64{10});
    damaged.close();

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, firstGeneration);
    QVERIFY(store.lastLoadRecoveredFromBackup());
}

} // namespace

QTEST_MAIN(PortForwardingRuleStoreTests)

#include "port_forwarding_rule_store_tests.moc"
