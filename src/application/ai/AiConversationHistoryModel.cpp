#include "application/ai/AiConversationHistoryModel.h"

#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace ztermy::ai
{
namespace
{
[[nodiscard]] QString normalizedLocalPath(const QString &path)
{
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}

template <typename Callable>
void postToGuiThread(QObject *receiver, Callable &&callable)
{
    // Qt takes ownership of the copied functor until queued delivery. The
    // analyzer cannot model that ownership transfer through invokeMethodImpl.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    const bool queued = QMetaObject::invokeMethod(receiver, std::forward<Callable>(callable), Qt::QueuedConnection);
    Q_ASSERT(queued);
}
} // namespace

AiConversationHistoryModel::AiConversationHistoryModel(QString filePath, security::CredentialVault &vault,
                                                       QObject *parent)
    : QAbstractListModel(parent), m_filePath(std::move(filePath)), m_vault(&vault)
{
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(-1);
}

AiConversationHistoryModel::~AiConversationHistoryModel()
{
    m_worker.waitForDone();
}

int AiConversationHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_conversations.size());
}

QVariant AiConversationHistoryModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }
    const auto &conversation = m_conversations.at(static_cast<std::size_t>(index.row()));
    switch (role)
    {
        case ConversationIdRole:
            return conversation.id;
        case TitleRole:
            return conversation.title;
        case UpdatedAtRole:
            return conversation.updatedAtUtc;
        case MessageCountRole:
            return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(
                std::ranges::count_if(conversation.messages, [](const AiStoredMessage &message) {
                    return message.role != QStringLiteral("evidence");
                })));
        case PreviewRole:
        {
            const auto visible = std::ranges::find_if(conversation.messages.rbegin(), conversation.messages.rend(),
                                                      [](const AiStoredMessage &message) {
                                                          return message.role != QStringLiteral("evidence");
                                                      });
            return visible == conversation.messages.rend() ? QString{} : visible->text.left(180);
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> AiConversationHistoryModel::roleNames() const
{
    return {{ConversationIdRole, "conversationId"},
            {TitleRole, "title"},
            {UpdatedAtRole, "updatedAt"},
            {MessageCountRole, "messageCount"},
            {PreviewRole, "preview"}};
}

bool AiConversationHistoryModel::busy() const noexcept
{
    return m_pendingOperations > 0;
}

QString AiConversationHistoryModel::errorCode() const
{
    return m_errorCode;
}

std::optional<AiStoredConversation> AiConversationHistoryModel::conversation(const QString &conversationId) const
{
    const auto found = std::ranges::find(m_conversations, conversationId, &AiStoredConversation::id);
    return found == m_conversations.end() ? std::nullopt : std::optional{*found};
}

void AiConversationHistoryModel::setVault(security::CredentialVault &vault)
{
    m_worker.waitForDone();
    ++m_generation;
    m_vault = &vault;
}

void AiConversationHistoryModel::reload()
{
    beginOperation();
    const QString filePath = m_filePath;
    security::CredentialVault *vault = m_vault;
    const quint64 generation = m_generation;
    m_worker.start([this, filePath, vault, generation] {
        try
        {
            auto loaded = AiConversationStore(filePath, *vault).load();
            if (!loaded)
            {
                const auto error = loaded.error();
                postToGuiThread(this, [this, error, generation] {
                    finishOperation(QStringLiteral("reload"), error, std::nullopt, generation);
                });
                return;
            }
            postToGuiThread(this, [this, conversations = std::move(*loaded), generation]() mutable {
                finishOperation(QStringLiteral("reload"), std::nullopt, std::move(conversations), generation);
            });
        }
        catch (...)
        {
            postToGuiThread(this, [this, generation] {
                finishOperation(QStringLiteral("reload"), AiConversationStoreError::unavailable, std::nullopt,
                                generation);
            });
        }
    });
}

void AiConversationHistoryModel::persist(AiStoredConversation conversation)
{
    beginOperation();
    const QString filePath = m_filePath;
    security::CredentialVault *vault = m_vault;
    const quint64 generation = m_generation;
    m_worker.start([this, filePath, vault, conversation = std::move(conversation), generation]() mutable {
        try
        {
            auto result = AiConversationStore(filePath, *vault).upsert(std::move(conversation));
            if (!result)
            {
                const auto error = result.error();
                postToGuiThread(this, [this, error, generation] {
                    finishOperation(QStringLiteral("persist"), error, std::nullopt, generation);
                });
                return;
            }
            auto loaded = AiConversationStore(filePath, *vault).load();
            if (!loaded)
            {
                const auto error = loaded.error();
                postToGuiThread(this, [this, error, generation] {
                    finishOperation(QStringLiteral("persist"), error, std::nullopt, generation);
                });
                return;
            }
            postToGuiThread(this, [this, conversations = std::move(*loaded), generation]() mutable {
                finishOperation(QStringLiteral("persist"), std::nullopt, std::move(conversations), generation);
            });
        }
        catch (...)
        {
            postToGuiThread(this, [this, generation] {
                finishOperation(QStringLiteral("persist"), AiConversationStoreError::unavailable, std::nullopt,
                                generation);
            });
        }
    });
}

void AiConversationHistoryModel::forgetLoaded()
{
    m_worker.waitForDone();
    ++m_generation;
    beginResetModel();
    m_conversations.clear();
    endResetModel();
    emit countChanged();
    if (!m_errorCode.isEmpty())
    {
        m_errorCode.clear();
        emit errorCodeChanged();
    }
}

void AiConversationHistoryModel::remove(const QString &conversationId)
{
    beginOperation();
    const QString filePath = m_filePath;
    security::CredentialVault *vault = m_vault;
    const quint64 generation = m_generation;
    m_worker.start([this, filePath, vault, conversationId, generation] {
        try
        {
            auto result = AiConversationStore(filePath, *vault).erase(conversationId);
            if (!result)
            {
                const auto error = result.error();
                postToGuiThread(this, [this, error, generation] {
                    finishOperation(QStringLiteral("remove"), error, std::nullopt, generation);
                });
                return;
            }
            auto loaded = AiConversationStore(filePath, *vault).load();
            if (!loaded)
            {
                const auto error = loaded.error();
                postToGuiThread(this, [this, error, generation] {
                    finishOperation(QStringLiteral("remove"), error, std::nullopt, generation);
                });
                return;
            }
            postToGuiThread(this, [this, conversations = std::move(*loaded), generation]() mutable {
                finishOperation(QStringLiteral("remove"), std::nullopt, std::move(conversations), generation);
            });
        }
        catch (...)
        {
            postToGuiThread(this, [this, generation] {
                finishOperation(QStringLiteral("remove"), AiConversationStoreError::unavailable, std::nullopt,
                                generation);
            });
        }
    });
}

void AiConversationHistoryModel::clear()
{
    beginOperation();
    const QString filePath = m_filePath;
    security::CredentialVault *vault = m_vault;
    const quint64 generation = m_generation;
    m_worker.start([this, filePath, vault, generation] {
        try
        {
            auto result = AiConversationStore(filePath, *vault).clear();
            const auto error = result ? std::optional<AiConversationStoreError>{}
                                      : std::optional<AiConversationStoreError>{result.error()};
            postToGuiThread(this, [this, error, generation] {
                finishOperation(
                    QStringLiteral("clear"), error,
                    error.has_value()
                        ? std::nullopt
                        : std::optional<std::vector<AiStoredConversation>>{std::vector<AiStoredConversation>{}},
                    generation);
            });
        }
        catch (...)
        {
            postToGuiThread(this, [this, generation] {
                finishOperation(QStringLiteral("clear"), AiConversationStoreError::unavailable, std::nullopt,
                                generation);
            });
        }
    });
}

void AiConversationHistoryModel::exportDecrypted(const QString &destinationPath)
{
    beginOperation();
    const QString filePath = m_filePath;
    const QString localPath = normalizedLocalPath(destinationPath);
    security::CredentialVault *vault = m_vault;
    const quint64 generation = m_generation;
    m_worker.start([this, filePath, localPath, vault, generation] {
        try
        {
            const auto result = AiConversationStore(filePath, *vault).exportDecrypted(localPath);
            const auto error = result ? std::optional<AiConversationStoreError>{}
                                      : std::optional<AiConversationStoreError>{result.error()};
            postToGuiThread(this, [this, error, generation] {
                finishOperation(QStringLiteral("export"), error, std::nullopt, generation);
            });
        }
        catch (...)
        {
            postToGuiThread(this, [this, generation] {
                finishOperation(QStringLiteral("export"), AiConversationStoreError::unavailable, std::nullopt,
                                generation);
            });
        }
    });
}

void AiConversationHistoryModel::beginOperation()
{
    const bool wasBusy = busy();
    ++m_pendingOperations;
    if (!wasBusy)
    {
        emit busyChanged();
    }
}

void AiConversationHistoryModel::finishOperation(QString operation, const std::optional<AiConversationStoreError> error,
                                                 std::optional<std::vector<AiStoredConversation>> conversations,
                                                 const quint64 generation)
{
    const bool wasBusy = busy();
    m_pendingOperations = std::max(0, m_pendingOperations - 1);
    const bool current = generation == m_generation;
    if (current && conversations.has_value())
    {
        beginResetModel();
        m_conversations = std::move(*conversations);
        endResetModel();
        emit countChanged();
    }
    const QString nextError = current && error.has_value() ? errorToken(*error) : QString{};
    if (current && nextError != m_errorCode)
    {
        m_errorCode = nextError;
        emit errorCodeChanged();
    }
    if (wasBusy != busy())
    {
        emit busyChanged();
    }
    emit operationFinished(std::move(operation), current && !error.has_value());
}

QString AiConversationHistoryModel::errorToken(const AiConversationStoreError error)
{
    switch (error)
    {
        case AiConversationStoreError::invalidPath:
            return QStringLiteral("invalid_path");
        case AiConversationStoreError::invalidData:
            return QStringLiteral("invalid_data");
        case AiConversationStoreError::unsupportedVersion:
            return QStringLiteral("unsupported_version");
        case AiConversationStoreError::locked:
            return QStringLiteral("vault_locked");
        case AiConversationStoreError::keyMissing:
            return QStringLiteral("key_missing");
        case AiConversationStoreError::authenticationFailed:
            return QStringLiteral("authentication_failed");
        case AiConversationStoreError::cryptoError:
            return QStringLiteral("crypto_error");
        case AiConversationStoreError::ioError:
            return QStringLiteral("io_error");
        case AiConversationStoreError::unavailable:
        default:
            return QStringLiteral("unavailable");
    }
}

} // namespace ztermy::ai
