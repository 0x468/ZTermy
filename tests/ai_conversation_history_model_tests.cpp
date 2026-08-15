#include "application/ai/AiConversationHistoryModel.h"

#include "core/security/SensitiveByteArray.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <ranges>

using namespace ztermy;

namespace
{
class TestVault final : public security::CredentialVault
{
public:
    explicit TestVault(const bool persistent = true) : m_persistent(persistent) {}

    [[nodiscard]] std::string_view backendId() const noexcept override { return "history-model-test"; }
    [[nodiscard]] bool persistent() const noexcept override { return m_persistent; }

    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    store(const security::CredentialKey &key, security::SensitiveByteArray secret) override
    {
        const QByteArray bytes(secret.view().data(), static_cast<qsizetype>(secret.view().size()));
        const auto found = std::ranges::find(m_records, key, &Record::key);
        if (found == m_records.end())
        {
            m_records.push_back({.key = key, .secret = bytes});
        }
        else
        {
            found->secret = bytes;
        }
        return {};
    }

    [[nodiscard]] std::expected<security::SensitiveByteArray, security::CredentialVaultError>
    read(const security::CredentialKey &key) const override
    {
        const auto found = std::ranges::find(m_records, key, &Record::key);
        if (found == m_records.end())
        {
            return std::unexpected(security::CredentialVaultError::NotFound);
        }
        return security::SensitiveByteArray(QByteArray(found->secret));
    }

    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    remove(const security::CredentialKey &key) override
    {
        return std::erase_if(m_records,
                             [&key](const Record &record) {
                                 return record.key == key;
                             })
                       > 0
                   ? std::expected<void, security::CredentialVaultError>{}
                   : std::unexpected(security::CredentialVaultError::NotFound);
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

private:
    struct Record final
    {
        security::CredentialKey key;
        QByteArray secret;
    };
    bool m_persistent = true;
    std::vector<Record> m_records;
};

[[nodiscard]] ai::AiStoredConversation storedConversation()
{
    return {.id = QStringLiteral("conversation-1"),
            .title = QStringLiteral("Explain the failure"),
            .agent = QStringLiteral("codex"),
            .externalThreadId = QStringLiteral("thread-history-1"),
            .updatedAtUtc = QDateTime::currentDateTimeUtc(),
            .messages = {{.role = QStringLiteral("user"), .text = QStringLiteral("question")},
                         {.role = QStringLiteral("assistant"), .text = QStringLiteral("answer")},
                         {.role = QStringLiteral("evidence"), .text = QStringLiteral("tool output")}}};
}
} // namespace

class AiConversationHistoryModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void persistsLoadsExportsAndRemovesOffTheCallerPath();
    void exposesUnavailablePersistentStorage();
    void vaultChangeInvalidatesQueuedPlaintext();
};

void AiConversationHistoryModelTests::persistsLoadsExportsAndRemovesOffTheCallerPath()
{
    QTemporaryDir directory;
    TestVault vault;
    ai::AiConversationHistoryModel model(directory.filePath(QStringLiteral("history.enc")), vault);
    QSignalSpy finished(&model, &ai::AiConversationHistoryModel::operationFinished);

    model.persist(storedConversation());
    QVERIFY(model.busy());
    QTRY_COMPARE(model.rowCount(), 1);
    QTRY_VERIFY(!model.busy());
    QVERIFY(model.errorCode().isEmpty());
    const auto restored = model.conversation(QStringLiteral("conversation-1"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored.value_or(ai::AiStoredConversation{}).messages.size(), 3ULL);
    QCOMPARE(model.data(model.index(0), ai::AiConversationHistoryModel::MessageCountRole).toULongLong(), 2ULL);
    QCOMPARE(model.data(model.index(0), ai::AiConversationHistoryModel::PreviewRole).toString(),
             QStringLiteral("answer"));
    QCOMPARE(model.data(model.index(0), ai::AiConversationHistoryModel::AgentRole).toString(), QStringLiteral("codex"));

    const QString exported = directory.filePath(QStringLiteral("history.json"));
    model.exportDecrypted(exported);
    QTRY_VERIFY(!model.busy());
    QFile file(exported);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("question"));

    model.remove(QStringLiteral("conversation-1"));
    QTRY_COMPARE(model.rowCount(), 0);
    QTRY_VERIFY(!model.busy());
    QVERIFY(finished.count() >= 3);
}

void AiConversationHistoryModelTests::exposesUnavailablePersistentStorage()
{
    QTemporaryDir directory;
    TestVault sessionVault(false);
    ai::AiConversationHistoryModel model(directory.filePath(QStringLiteral("history.enc")), sessionVault);
    model.persist(storedConversation());
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.errorCode(), QStringLiteral("unavailable"));
    QCOMPARE(model.rowCount(), 0);
}

void AiConversationHistoryModelTests::vaultChangeInvalidatesQueuedPlaintext()
{
    QTemporaryDir directory;
    TestVault originalVault;
    TestVault replacementVault;
    ai::AiConversationHistoryModel model(directory.filePath(QStringLiteral("history.enc")), originalVault);

    model.persist(storedConversation());
    model.setVault(replacementVault);
    model.forgetLoaded();

    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.conversation(QStringLiteral("conversation-1")).has_value());
}

QTEST_GUILESS_MAIN(AiConversationHistoryModelTests)

#include "ai_conversation_history_model_tests.moc"
