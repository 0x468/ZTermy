#include "infrastructure/sftp/TransferBatchRecoveryStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <optional>
#include <utility>

namespace ztermy::sftp
{
namespace
{

constexpr int formatVersion = 1;
constexpr qsizetype maximumDocumentBytes = qsizetype{128} * 1024 * 1024;

[[nodiscard]] QString qString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
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

[[nodiscard]] bool recoverable(const TransferBatch &batch) noexcept
{
    return batch.status == TransferBatchStatus::Ready || batch.status == TransferBatchStatus::Running
           || batch.status == TransferBatchStatus::Paused || batch.status == TransferBatchStatus::NeedsAttention
           || batch.status == TransferBatchStatus::Interrupted;
}

[[nodiscard]] QJsonObject entryObject(const TransferPlanEntry &entry)
{
    QJsonObject object{
        {QStringLiteral("id"), qString(entry.id)},
        {QStringLiteral("parentId"), qString(entry.parentId)},
        {QStringLiteral("relativePath"), qString(entry.relativePath)},
        {QStringLiteral("sourcePath"), qString(entry.sourcePath)},
        {QStringLiteral("childTaskId"), qString(entry.childTaskId)},
        {QStringLiteral("errorCode"), qString(entry.errorCode)},
        {QStringLiteral("kind"), static_cast<int>(entry.kind)},
        {QStringLiteral("status"), static_cast<int>(entry.status)},
        {QStringLiteral("totalBytes"), QString::number(entry.totalBytes)},
        {QStringLiteral("transferredBytes"), QString::number(entry.transferredBytes)},
        {QStringLiteral("depth"), static_cast<int>(entry.depth)},
    };
    object.insert(QStringLiteral("sourceModifiedUtcSeconds"),
                  entry.sourceModifiedUtcSeconds ? QJsonValue(QString::number(*entry.sourceModifiedUtcSeconds))
                                                 : QJsonValue::Null);
    return object;
}

[[nodiscard]] QJsonObject batchObject(const TransferBatch &batch)
{
    QJsonArray roots;
    for (const std::string &root : batch.sourceRoots)
    {
        roots.append(qString(root));
    }
    QJsonArray entries;
    for (const TransferPlanEntry &entry : batch.entries)
    {
        entries.append(entryObject(entry));
    }
    return {
        {QStringLiteral("id"), qString(batch.id)},
        {QStringLiteral("endpointId"), qString(batch.endpointId)},
        {QStringLiteral("displayName"), qString(batch.displayName)},
        {QStringLiteral("destinationRoot"), qString(batch.destinationRoot)},
        {QStringLiteral("sourceRoots"), std::move(roots)},
        {QStringLiteral("entries"), std::move(entries)},
        {QStringLiteral("direction"), static_cast<int>(batch.direction)},
        {QStringLiteral("conflictPolicy"), static_cast<int>(batch.conflictPolicy)},
        {QStringLiteral("applyConflictPolicyToRemaining"), batch.applyConflictPolicyToRemaining},
    };
}

template <typename Enum>
[[nodiscard]] std::optional<Enum> enumValue(const QJsonObject &object, const QString &key, const int maximum)
{
    const int value = object.value(key).toInt(-1);
    return value >= 0 && value <= maximum ? std::optional{static_cast<Enum>(value)} : std::nullopt;
}

[[nodiscard]] std::optional<TransferPlanEntry> parseEntry(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const auto kind = enumValue<TransferPlanEntryKind>(object, QStringLiteral("kind"),
                                                       static_cast<int>(TransferPlanEntryKind::Unsupported));
    const auto status = enumValue<TransferPlanEntryStatus>(object, QStringLiteral("status"),
                                                           static_cast<int>(TransferPlanEntryStatus::Interrupted));
    const auto totalBytes = unsignedValue(object, QStringLiteral("totalBytes"));
    const auto transferredBytes = unsignedValue(object, QStringLiteral("transferredBytes"));
    const int depth = object.value(QStringLiteral("depth")).toInt(-1);
    if (!kind || !status || !totalBytes || !transferredBytes || depth < 0
        || std::cmp_greater(depth, maximumTransferTreeDepth))
    {
        return std::nullopt;
    }
    TransferPlanEntry entry{
        .id = utf8String(object.value(QStringLiteral("id")).toString()),
        .parentId = utf8String(object.value(QStringLiteral("parentId")).toString()),
        .relativePath = utf8String(object.value(QStringLiteral("relativePath")).toString()),
        .sourcePath = utf8String(object.value(QStringLiteral("sourcePath")).toString()),
        .childTaskId = utf8String(object.value(QStringLiteral("childTaskId")).toString()),
        .errorCode = utf8String(object.value(QStringLiteral("errorCode")).toString()),
        .kind = *kind,
        .status = *status,
        .totalBytes = *totalBytes,
        .transferredBytes = *transferredBytes,
        .depth = static_cast<std::uint32_t>(depth),
        .sourceModifiedUtcSeconds = signedValue(object, QStringLiteral("sourceModifiedUtcSeconds")),
    };
    if (entry.status != TransferPlanEntryStatus::Completed && entry.status != TransferPlanEntryStatus::Skipped
        && entry.status != TransferPlanEntryStatus::Cancelled)
    {
        entry.status = TransferPlanEntryStatus::Interrupted;
        entry.errorCode.clear();
    }
    return validTransferPlanEntry(entry) ? std::optional{std::move(entry)} : std::nullopt;
}

[[nodiscard]] std::optional<TransferBatch> parseBatch(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue rootsValue = object.value(QStringLiteral("sourceRoots"));
    const QJsonValue entriesValue = object.value(QStringLiteral("entries"));
    const auto direction = enumValue<TransferBatchDirection>(object, QStringLiteral("direction"),
                                                             static_cast<int>(TransferBatchDirection::Download));
    const auto conflictPolicy = enumValue<TransferConflictPolicy>(object, QStringLiteral("conflictPolicy"),
                                                                  static_cast<int>(TransferConflictPolicy::Skip));
    if (!rootsValue.isArray() || !entriesValue.isArray() || !direction || !conflictPolicy
        || rootsValue.toArray().size() > static_cast<qsizetype>(maximumTransferSourceRoots)
        || entriesValue.toArray().size() > static_cast<qsizetype>(maximumTransferPlanEntries))
    {
        return std::nullopt;
    }
    TransferBatch batch{
        .id = utf8String(object.value(QStringLiteral("id")).toString()),
        .endpointId = utf8String(object.value(QStringLiteral("endpointId")).toString()),
        .displayName = utf8String(object.value(QStringLiteral("displayName")).toString()),
        .destinationRoot = utf8String(object.value(QStringLiteral("destinationRoot")).toString()),
        .direction = *direction,
        .status = TransferBatchStatus::Interrupted,
        .conflictPolicy = *conflictPolicy,
        .applyConflictPolicyToRemaining = object.value(QStringLiteral("applyConflictPolicyToRemaining")).toBool(),
    };
    for (const auto &root : rootsValue.toArray())
    {
        if (!root.isString())
        {
            return std::nullopt;
        }
        batch.sourceRoots.push_back(utf8String(root.toString()));
    }
    for (const auto &entryValue : entriesValue.toArray())
    {
        auto entry = parseEntry(entryValue);
        if (!entry)
        {
            return std::nullopt;
        }
        batch.entries.push_back(std::move(*entry));
    }
    return validTransferBatch(batch) ? std::optional{std::move(batch)} : std::nullopt;
}

} // namespace

TransferBatchRecoveryStore::TransferBatchRecoveryStore(QString path) : m_path(std::move(path)) {}

std::expected<std::vector<TransferBatch>, TransferBatchRecoveryError> TransferBatchRecoveryStore::load() const
{
    if (m_path.isEmpty() || !QFile::exists(m_path))
    {
        return std::vector<TransferBatch>{};
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumDocumentBytes)
    {
        return std::unexpected(TransferBatchRecoveryError::Io);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(TransferBatchRecoveryError::InvalidData);
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != formatVersion
        || !root.value(QStringLiteral("batches")).isArray())
    {
        return std::unexpected(TransferBatchRecoveryError::UnsupportedVersion);
    }
    const QJsonArray values = root.value(QStringLiteral("batches")).toArray();
    if (values.size() > static_cast<qsizetype>(maximumTransferBatches))
    {
        return std::unexpected(TransferBatchRecoveryError::InvalidData);
    }
    std::vector<TransferBatch> batches;
    batches.reserve(static_cast<std::size_t>(values.size()));
    for (const auto &value : values)
    {
        auto batch = parseBatch(value);
        if (!batch)
        {
            return std::unexpected(TransferBatchRecoveryError::InvalidData);
        }
        batches.push_back(std::move(*batch));
    }
    return batches;
}

std::expected<void, TransferBatchRecoveryError>
TransferBatchRecoveryStore::save(const std::span<const TransferBatch> batches) const
{
    if (m_path.isEmpty())
    {
        return {};
    }
    QJsonArray values;
    for (const TransferBatch &batch : batches)
    {
        if (!recoverable(batch))
        {
            continue;
        }
        if (values.size() >= static_cast<qsizetype>(maximumTransferBatches) || !validTransferBatch(batch))
        {
            return std::unexpected(TransferBatchRecoveryError::InvalidData);
        }
        values.append(batchObject(batch));
    }
    const QByteArray contents = QJsonDocument(QJsonObject{{QStringLiteral("version"), formatVersion},
                                                          {QStringLiteral("batches"), std::move(values)}})
                                    .toJson(QJsonDocument::Compact);
    if (contents.size() > maximumDocumentBytes)
    {
        return std::unexpected(TransferBatchRecoveryError::InvalidData);
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    {
        return std::unexpected(TransferBatchRecoveryError::Io);
    }
    return {};
}

} // namespace ztermy::sftp
