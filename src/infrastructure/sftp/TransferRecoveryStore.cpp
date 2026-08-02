#include "infrastructure/sftp/TransferRecoveryStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ztermy::sftp
{
namespace
{

constexpr int formatVersion = 1;
constexpr qsizetype maximumDocumentBytes = qsizetype{1024} * 1024;
constexpr qsizetype maximumRecoveredTasks = 100;

[[nodiscard]] QString qString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] bool boundedString(const QString &value, const qsizetype maximumBytes, const bool allowEmpty = false)
{
    const QByteArray bytes = value.toUtf8();
    return (allowEmpty || !bytes.isEmpty()) && bytes.size() <= maximumBytes && !value.contains(QChar::Null);
}

[[nodiscard]] std::optional<std::uint64_t> unsignedValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
    {
        return std::nullopt;
    }
    bool okay = false;
    const qulonglong parsed = value.toString().toULongLong(&okay);
    return okay ? std::optional{static_cast<std::uint64_t>(parsed)} : std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> signedValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isNull() || value.isUndefined())
    {
        return std::nullopt;
    }
    if (!value.isString())
    {
        return std::nullopt;
    }
    bool okay = false;
    const qlonglong parsed = value.toString().toLongLong(&okay);
    return okay ? std::optional{static_cast<std::int64_t>(parsed)} : std::nullopt;
}

[[nodiscard]] bool recoverable(const TransferTask &task)
{
    return task.status == TransferStatus::Queued || task.status == TransferStatus::Running
           || task.status == TransferStatus::Cancelling || task.status == TransferStatus::NeedsAttention
           || (task.status == TransferStatus::Failed && task.retryable && task.errorCode == "interrupted");
}

[[nodiscard]] QJsonObject taskObject(const TransferTask &task)
{
    QJsonObject object{
        {QStringLiteral("id"), qString(task.id)},
        {QStringLiteral("endpointId"), qString(task.endpointId)},
        {QStringLiteral("displayName"), qString(task.displayName)},
        {QStringLiteral("sourcePath"), qString(task.sourcePath)},
        {QStringLiteral("destinationPath"), qString(task.destinationPath)},
        {QStringLiteral("direction"),
         task.direction == TransferDirection::Download ? QStringLiteral("download") : QStringLiteral("upload")},
        {QStringLiteral("totalBytes"), QString::number(task.totalBytes)},
        {QStringLiteral("transferredBytes"), QString::number(task.transferredBytes)},
    };
    object.insert(QStringLiteral("startedUtcMs"),
                  task.startedUtcMs ? QJsonValue(QString::number(*task.startedUtcMs)) : QJsonValue::Null);
    return object;
}

[[nodiscard]] std::optional<TransferTask> parseTask(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString id = object.value(QStringLiteral("id")).toString();
    const QString endpointId = object.value(QStringLiteral("endpointId")).toString();
    const QString displayName = object.value(QStringLiteral("displayName")).toString();
    const QString sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    const QString destinationPath = object.value(QStringLiteral("destinationPath")).toString();
    const QString direction = object.value(QStringLiteral("direction")).toString();
    const auto totalBytes = unsignedValue(object, QStringLiteral("totalBytes"));
    const auto transferredBytes = unsignedValue(object, QStringLiteral("transferredBytes"));
    if (!boundedString(id, 128) || !boundedString(endpointId, 128) || !boundedString(displayName, 512)
        || !boundedString(sourcePath, 4096) || !boundedString(destinationPath, 4096)
        || (direction != QStringLiteral("download") && direction != QStringLiteral("upload")) || !totalBytes
        || !transferredBytes || (*totalBytes != 0 && *transferredBytes > *totalBytes))
    {
        return std::nullopt;
    }

    TransferTask task{
        .id = utf8String(id),
        .endpointId = utf8String(endpointId),
        .displayName = utf8String(displayName),
        .sourcePath = utf8String(sourcePath),
        .destinationPath = utf8String(destinationPath),
        .direction = direction == QStringLiteral("download") ? TransferDirection::Download : TransferDirection::Upload,
        .status = TransferStatus::Failed,
        .totalBytes = *totalBytes,
        .transferredBytes = *transferredBytes,
        .startedUtcMs = signedValue(object, QStringLiteral("startedUtcMs")),
        .errorCode = "interrupted",
        .retryable = true,
    };
    return validTransferTask(task) ? std::optional{std::move(task)} : std::nullopt;
}

} // namespace

TransferRecoveryStore::TransferRecoveryStore(QString path) : m_path(std::move(path)) {}

const QString &TransferRecoveryStore::path() const noexcept
{
    return m_path;
}

std::expected<std::vector<TransferTask>, TransferRecoveryError> TransferRecoveryStore::load() const
{
    if (m_path.isEmpty() || !QFile::exists(m_path))
    {
        return std::vector<TransferTask>{};
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumDocumentBytes)
    {
        return std::unexpected(TransferRecoveryError::Io);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(TransferRecoveryError::InvalidData);
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != formatVersion)
    {
        return std::unexpected(TransferRecoveryError::UnsupportedVersion);
    }
    const QJsonValue tasksValue = root.value(QStringLiteral("tasks"));
    if (!tasksValue.isArray() || tasksValue.toArray().size() > maximumRecoveredTasks)
    {
        return std::unexpected(TransferRecoveryError::InvalidData);
    }
    const QJsonArray taskValues = tasksValue.toArray();
    std::vector<TransferTask> tasks;
    tasks.reserve(static_cast<std::size_t>(taskValues.size()));
    for (const auto value : taskValues)
    {
        auto task = parseTask(value);
        if (!task)
        {
            return std::unexpected(TransferRecoveryError::InvalidData);
        }
        tasks.push_back(std::move(*task));
    }
    return tasks;
}

std::expected<void, TransferRecoveryError> TransferRecoveryStore::save(const std::span<const TransferTask> tasks) const
{
    if (m_path.isEmpty())
    {
        return {};
    }
    QJsonArray values;
    for (const TransferTask &task : tasks)
    {
        if (!recoverable(task))
        {
            continue;
        }
        if (values.size() >= maximumRecoveredTasks || !validTransferTask(task))
        {
            return std::unexpected(TransferRecoveryError::InvalidData);
        }
        values.append(taskObject(task));
    }
    const QJsonDocument document(
        QJsonObject{{QStringLiteral("version"), formatVersion}, {QStringLiteral("tasks"), std::move(values)}});
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(document.toJson(QJsonDocument::Compact)) < 0 || !file.commit())
    {
        return std::unexpected(TransferRecoveryError::Io);
    }
    return {};
}

} // namespace ztermy::sftp
