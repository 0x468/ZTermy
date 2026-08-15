#include "infrastructure/ai/AiConversationStore.h"

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderReplayCodec.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <utility>

using namespace ztermy;

namespace
{
class TestVault final : public security::CredentialVault
{
public:
    explicit TestVault(const bool persistent = true) : m_persistent(persistent) {}

    [[nodiscard]] std::string_view backendId() const noexcept override { return "test"; }
    [[nodiscard]] bool persistent() const noexcept override { return m_persistent; }

    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    store(const security::CredentialKey &key, security::SensitiveByteArray secret) override
    {
        const QByteArray bytes(secret.view().data(), static_cast<qsizetype>(secret.view().size()));
        const auto existing = std::ranges::find(m_records, key, &Record::key);
        if (existing == m_records.end())
        {
            m_records.push_back({.key = key, .secret = bytes});
        }
        else
        {
            existing->secret = bytes;
        }
        return {};
    }

    [[nodiscard]] std::expected<security::SensitiveByteArray, security::CredentialVaultError>
    read(const security::CredentialKey &key) const override
    {
        const auto existing = std::ranges::find(m_records, key, &Record::key);
        if (existing == m_records.end())
        {
            return std::unexpected(security::CredentialVaultError::NotFound);
        }
        return security::SensitiveByteArray(QByteArray(existing->secret));
    }

    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    remove(const security::CredentialKey &key) override
    {
        const auto removed = std::erase_if(m_records, [&key](const Record &record) {
            return record.key == key;
        });
        return removed == 0 ? std::unexpected(security::CredentialVaultError::NotFound)
                            : std::expected<void, security::CredentialVaultError>{};
    }

    [[nodiscard]] std::expected<std::vector<security::CredentialKey>, security::CredentialVaultError>
    listKeys() const override
    {
        std::vector<security::CredentialKey> keys;
        keys.reserve(m_records.size());
        for (const auto &record : m_records)
        {
            keys.push_back(record.key);
        }
        return keys;
    }

    void forgetAll() { m_records.clear(); }

private:
    struct Record final
    {
        security::CredentialKey key;
        QByteArray secret;
    };

    bool m_persistent = true;
    std::vector<Record> m_records;
};

[[nodiscard]] ai::AiStoredConversation conversation(const QString &id, const QString &text, const int dayOffset = 0)
{
    return {.id = id,
            .title = QStringLiteral("Conversation ") + id,
            .updatedAtUtc = QDateTime::currentDateTimeUtc().addDays(dayOffset),
            .messages = {{.role = QStringLiteral("user"), .text = text},
                         {.role = QStringLiteral("assistant"), .text = QStringLiteral("answer-") + text},
                         {.role = QStringLiteral("evidence"), .text = QStringLiteral("tool-") + text}}};
}

[[nodiscard]] QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
} // namespace

class AiConversationStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsWithoutPlaintextAndRotatesNonce();
    void rejectsTamperingAndMissingKeys();
    void enforcesRetentionAndConversationBounds();
    void rejectsUnsupportedEnvelopeAndSessionVault();
    void exportsPlaintextAndDeletesStoreAndKey();
    void recoversFromBackupOnAuthenticationFailure();
    void preservesUpdatesAcrossStoreInstances();
    void roundTripsAndValidatesWebSources();
    void roundTripsAndValidatesProviderReplay();
    void roundTripsAgentTurnPresentation();
};

void AiConversationStoreTests::roundTripsWithoutPlaintextAndRotatesNonce()
{
    QTemporaryDir directory;
    TestVault vault;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    ai::AiConversationStore store(path, vault);

    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("secret-command"))).has_value());
    const QByteArray firstEnvelope = readFile(path);
    QVERIFY(!firstEnvelope.contains("secret-command"));
    const auto firstObject = QJsonDocument::fromJson(firstEnvelope).object();
    QCOMPARE(firstObject.value(QStringLiteral("generation")).toInt(), 1);
    const QString firstNonce = firstObject.value(QStringLiteral("nonce")).toString();

    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("updated-secret"))).has_value());
    const auto secondObject = QJsonDocument::fromJson(readFile(path)).object();
    QCOMPARE(secondObject.value(QStringLiteral("generation")).toInt(), 2);
    QVERIFY(secondObject.value(QStringLiteral("nonce")).toString() != firstNonce);

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), 1ULL);
    QCOMPARE(loaded->front().messages.front().text, QStringLiteral("updated-secret"));
    QCOMPARE(loaded->front().messages.back().role, QStringLiteral("evidence"));

    auto codex = conversation(QStringLiteral("codex"), QStringLiteral("resume-agent"));
    codex.agent = QStringLiteral("codex");
    codex.externalThreadId = QStringLiteral("thread-codex-1");
    QVERIFY(store.upsert(codex).has_value());
    const auto withAgent = store.load();
    QVERIFY(withAgent.has_value());
    const auto restoredCodex =
        std::ranges::find(withAgent.value(), QStringLiteral("codex"), &ai::AiStoredConversation::id);
    QVERIFY(restoredCodex != withAgent->end());
    QCOMPARE(restoredCodex->agent, QStringLiteral("codex"));
    QCOMPARE(restoredCodex->externalThreadId, QStringLiteral("thread-codex-1"));

    codex.agent = QStringLiteral("unknown-agent");
    QVERIFY(!store.upsert(codex).has_value());
    codex.agent = QStringLiteral("ztermy");
    QVERIFY(!store.upsert(codex).has_value());
}

void AiConversationStoreTests::rejectsTamperingAndMissingKeys()
{
    QTemporaryDir directory;
    TestVault vault;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    ai::AiConversationStore store(path, vault);
    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("secret"))).has_value());

    auto envelope = QJsonDocument::fromJson(readFile(path)).object();
    QByteArray ciphertext = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
    QVERIFY(!ciphertext.isEmpty());
    ciphertext[0] = static_cast<char>(ciphertext[0] ^ 0x1);
    envelope.insert(QStringLiteral("ciphertext"), QString::fromLatin1(ciphertext.toBase64()));
    QVERIFY(writeFile(path, QJsonDocument(envelope).toJson(QJsonDocument::Compact)));
    const auto tampered = store.load();
    QVERIFY(!tampered.has_value());
    QCOMPARE(tampered.error(), ai::AiConversationStoreError::authenticationFailed);

    QVERIFY(store.clear().has_value());
    QVERIFY(store.upsert(conversation(QStringLiteral("two"), QStringLiteral("secret-two"))).has_value());
    vault.forgetAll();
    const auto missing = store.load();
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error(), ai::AiConversationStoreError::keyMissing);
}

void AiConversationStoreTests::enforcesRetentionAndConversationBounds()
{
    QTemporaryDir directory;
    TestVault vault;
    ai::AiConversationStore store(directory.filePath(QStringLiteral("history.enc")), vault,
                                  {.maximumConversations = 2,
                                   .maximumMessagesPerConversation = 4,
                                   .maximumMessageBytes = 1024,
                                   .maximumPlaintextBytes = std::size_t{16} * 1024,
                                   .retentionDays = 30});
    QVERIFY(store.upsert(conversation(QStringLiteral("old"), QStringLiteral("old"), -40)).has_value());
    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("one"), -2)).has_value());
    QVERIFY(store.upsert(conversation(QStringLiteral("two"), QStringLiteral("two"), -1)).has_value());
    QVERIFY(store.upsert(conversation(QStringLiteral("three"), QStringLiteral("three"))).has_value());

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), 2ULL);
    QCOMPARE(loaded->at(0).id, QStringLiteral("three"));
    QCOMPARE(loaded->at(1).id, QStringLiteral("two"));
}

void AiConversationStoreTests::rejectsUnsupportedEnvelopeAndSessionVault()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    QVERIFY(writeFile(
        path, QByteArrayLiteral("{\"version\":99,\"generation\":1,\"nonce\":\"\",\"ciphertext\":\"\",\"tag\":\"\"}")));
    TestVault vault;
    ai::AiConversationStore store(path, vault);
    const auto unsupported = store.load();
    QVERIFY(!unsupported.has_value());
    QCOMPARE(unsupported.error(), ai::AiConversationStoreError::unsupportedVersion);

    TestVault sessionVault(false);
    ai::AiConversationStore sessionStore(directory.filePath(QStringLiteral("session.enc")), sessionVault);
    const auto unavailable = sessionStore.upsert(conversation(QStringLiteral("one"), QStringLiteral("secret")));
    QVERIFY(!unavailable.has_value());
    QCOMPARE(unavailable.error(), ai::AiConversationStoreError::unavailable);
}

void AiConversationStoreTests::exportsPlaintextAndDeletesStoreAndKey()
{
    QTemporaryDir directory;
    TestVault vault;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    ai::AiConversationStore store(path, vault);
    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("export-secret"))).has_value());
    const QString exportPath = directory.filePath(QStringLiteral("history.json"));
    QVERIFY(store.exportDecrypted(exportPath).has_value());
    QVERIFY(readFile(exportPath).contains("export-secret"));

    QVERIFY(store.clear().has_value());
    QVERIFY(!QFileInfo::exists(path));
    const auto keys = vault.listKeys();
    QVERIFY(keys.has_value());
    QVERIFY(keys->empty());
}

void AiConversationStoreTests::recoversFromBackupOnAuthenticationFailure()
{
    QTemporaryDir directory;
    TestVault vault;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    ai::AiConversationStore store(path, vault);
    QVERIFY(store.upsert(conversation(QStringLiteral("one"), QStringLiteral("first"))).has_value());
    // A second save backs up the first envelope into .bak.
    QVERIFY(store.upsert(conversation(QStringLiteral("two"), QStringLiteral("second"))).has_value());
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));

    // Corrupt the primary ciphertext while keeping the envelope structure
    // valid (the exact case the LKG structural validator cannot detect).
    auto envelope = QJsonDocument::fromJson(readFile(path)).object();
    QByteArray ciphertext = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
    ciphertext[0] = static_cast<char>(ciphertext[0] ^ 0x1);
    envelope.insert(QStringLiteral("ciphertext"), QString::fromLatin1(ciphertext.toBase64()));
    QVERIFY(writeFile(path, QJsonDocument(envelope).toJson(QJsonDocument::Compact)));

    QVERIFY(!store.lastLoadRecoveredFromBackup());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(store.lastLoadRecoveredFromBackup());
    // The backup holds the previous successful save (last-known-good), so the
    // recovered history contains the state before the second upsert.
    QCOMPARE(loaded->size(), 1ULL);
    QCOMPARE(loaded->front().messages.front().text, QStringLiteral("first"));
}

void AiConversationStoreTests::preservesUpdatesAcrossStoreInstances()
{
    QTemporaryDir directory;
    TestVault vault;
    const QString path = directory.filePath(QStringLiteral("history.enc"));
    ai::AiConversationStore first(path, vault);
    ai::AiConversationStore second(path, vault);

    // Interleaved read-modify-write cycles from two store objects (the
    // cross-instance equivalent of two application processes) must not drop
    // either update; the file lock serializes the cycles.
    QVERIFY(first.upsert(conversation(QStringLiteral("one"), QStringLiteral("secret-one"))).has_value());
    QVERIFY(second.upsert(conversation(QStringLiteral("two"), QStringLiteral("secret-two"))).has_value());
    QVERIFY(first.upsert(conversation(QStringLiteral("three"), QStringLiteral("secret-three"))).has_value());

    const auto loaded = first.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), 3ULL);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
}

void AiConversationStoreTests::roundTripsAndValidatesWebSources()
{
    QTemporaryDir directory;
    TestVault vault;
    ai::AiConversationStore store(directory.filePath(QStringLiteral("history.enc")), vault);
    auto value = conversation(QStringLiteral("sources"), QStringLiteral("question"));
    value.messages.at(1).sources = {
        {.url = "https://example.test/reference", .title = "Primary reference", .citedText = "The cited passage."}};
    QVERIFY(store.upsert(value).has_value());

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->front().messages.at(1).sources, value.messages.at(1).sources);

    value.messages.at(1).sources.front().url = "file:///C:/private.txt";
    const auto invalidScheme = store.upsert(value);
    QVERIFY(!invalidScheme.has_value());
    QCOMPARE(invalidScheme.error(), ai::AiConversationStoreError::invalidData);

    value.messages.at(1).sources.front().url = "https://example.test/reference";
    value.messages.front().sources = value.messages.at(1).sources;
    const auto sourcesOnUserMessage = store.upsert(value);
    QVERIFY(!sourcesOnUserMessage.has_value());
    QCOMPARE(sourcesOnUserMessage.error(), ai::AiConversationStoreError::invalidData);
}

void AiConversationStoreTests::roundTripsAndValidatesProviderReplay()
{
    QTemporaryDir directory;
    TestVault vault;
    ai::AiConversationStore store(directory.filePath(QStringLiteral("history.enc")), vault);
    auto value = conversation(QStringLiteral("replay"), QStringLiteral("question"));
    const std::vector history{ai::AiToolExchange{
        .calls = {ai::AiToolCall{.id = "tool_1", .name = "run_command", .argumentsJson = R"({"command":"pwd"})"}},
        .outputs = {ai::AiToolOutput{.callId = "tool_1", .name = "run_command", .outputJson = R"({"ok":true})"}}}};
    const auto replay = ai::AiProviderReplayCodec::encode(
        history, R"([{"type":"text","text":"The working directory is /home/test."}])");
    QVERIFY(replay.has_value());
    value.messages.at(1).providerReplayJson = *replay;
    QVERIFY(store.upsert(value).has_value());

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->front().messages.at(1).providerReplayJson, *replay);

    value.messages.front().providerReplayJson = *replay;
    const auto replayOnUser = store.upsert(value);
    QVERIFY(!replayOnUser.has_value());
    QCOMPARE(replayOnUser.error(), ai::AiConversationStoreError::invalidData);

    value.messages.front().providerReplayJson.clear();
    value.messages.at(1).providerReplayJson = "{}";
    const auto malformed = store.upsert(value);
    QVERIFY(!malformed.has_value());
    QCOMPARE(malformed.error(), ai::AiConversationStoreError::invalidData);
}

void AiConversationStoreTests::roundTripsAgentTurnPresentation()
{
    QTemporaryDir directory;
    TestVault vault;
    ai::AiConversationStore store(directory.filePath(QStringLiteral("history.enc")), vault);
    auto value = conversation(QStringLiteral("presentation"), QStringLiteral("inspect disks"));
    auto &assistant = value.messages.at(1);
    assistant.reasoning = QStringLiteral("Inspect the filesystem table first.");
    assistant.toolActivities = {{.id = "tool-1",
                                 .name = "run_command",
                                 .summary = "df -h",
                                 .state = "succeeded",
                                 .resultCode = "ok",
                                 .sideEffecting = true}};
    assistant.usage =
        ai::AiTokenUsage{.inputTokens = 50, .outputTokens = 12, .reasoningTokens = 7, .cachedInputTokens = 4};
    assistant.metrics = ai::AiTurnMetrics{.wallTimeMilliseconds = 820, .firstTokenMilliseconds = 145, .retryCount = 1};
    assistant.estimatedCostUsd = 0.00073;
    assistant.costCatalogDate = QStringLiteral("2026-08-15");
    assistant.longContextRates = true;
    assistant.truncated = true;

    QVERIFY(store.upsert(value).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->front().messages.at(1) == assistant);

    value.messages.front().toolActivities = assistant.toolActivities;
    const auto metadataOnUser = store.upsert(value);
    QVERIFY(!metadataOnUser.has_value());
    QCOMPARE(metadataOnUser.error(), ai::AiConversationStoreError::invalidData);

    value.messages.front().toolActivities.clear();
    value.messages.at(1).toolActivities.front().name.clear();
    const auto invalidActivity = store.upsert(value);
    QVERIFY(!invalidActivity.has_value());
    QCOMPARE(invalidActivity.error(), ai::AiConversationStoreError::invalidData);
}

QTEST_GUILESS_MAIN(AiConversationStoreTests)

#include "ai_conversation_store_tests.moc"
