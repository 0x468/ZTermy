#pragma once

#include "infrastructure/ai/AiConversationStore.h"

#include <QAbstractListModel>
#include <QThreadPool>

#include <optional>
#include <vector>

namespace ztermy::ai
{

class AiConversationHistoryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorCodeChanged)

public:
    enum Role : int // NOLINT(performance-enum-size) Qt model roles are int.
    {
        ConversationIdRole = Qt::UserRole + 1,
        TitleRole,
        UpdatedAtRole,
        MessageCountRole,
        PreviewRole,
        AgentRole,
    };

    AiConversationHistoryModel(QString filePath, security::CredentialVault &vault, QObject *parent = nullptr);
    ~AiConversationHistoryModel() override;

    AiConversationHistoryModel(const AiConversationHistoryModel &) = delete;
    AiConversationHistoryModel &operator=(const AiConversationHistoryModel &) = delete;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString errorCode() const;
    [[nodiscard]] std::optional<AiStoredConversation> conversation(const QString &conversationId) const;

    void setVault(security::CredentialVault &vault);
    void reload();
    void persist(AiStoredConversation conversation);
    void forgetLoaded();
    Q_INVOKABLE void remove(const QString &conversationId);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void exportDecrypted(const QString &destinationPath);

signals:
    void countChanged();
    void busyChanged();
    void errorCodeChanged();
    void operationFinished(QString operation, bool succeeded);

private:
    void beginOperation();
    void finishOperation(QString operation, std::optional<AiConversationStoreError> error,
                         std::optional<std::vector<AiStoredConversation>> conversations, quint64 generation);
    [[nodiscard]] static QString errorToken(AiConversationStoreError error);

    QString m_filePath;
    security::CredentialVault *m_vault = nullptr;
    std::vector<AiStoredConversation> m_conversations;
    QThreadPool m_worker;
    QString m_errorCode;
    int m_pendingOperations = 0;
    quint64 m_generation = 0;
};

} // namespace ztermy::ai
