#include "infrastructure/ai/AiConversationStore.h"

#include "core/security/SensitiveByteArray.h"

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

QTEST_GUILESS_MAIN(AiConversationStoreTests)

#include "ai_conversation_store_tests.moc"
