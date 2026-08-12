#include "application/ai/AiActivityModel.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>
#include <memory>
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr int currentSchemaVersion = 1;
constexpr qsizetype maximumDocumentBytes = qsizetype{512} * 1024;

[[nodiscard]] persistence::PayloadValidation validatePayload(const QByteArrayView payload)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(payload.data(), payload.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return persistence::PayloadValidation::invalid;
    }
    const auto object = document.object();
    if (!object.value(QStringLiteral("schemaVersion")).isDouble())
    {
        return persistence::PayloadValidation::invalid;
    }
    if (object.value(QStringLiteral("schemaVersion")).toInt() != currentSchemaVersion)
    {
        return persistence::PayloadValidation::unsupportedVersion;
    }
    return object.value(QStringLiteral("records")).isArray() ? persistence::PayloadValidation::valid
                                                             : persistence::PayloadValidation::invalid;
}

[[nodiscard]] QString loadErrorText(const persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case persistence::LastKnownGoodError::unsupportedVersion:
            return QStringLiteral("unsupported_version");
        case persistence::LastKnownGoodError::invalidFormat:
            return QStringLiteral("invalid_format");
        case persistence::LastKnownGoodError::invalidPath:
            return QStringLiteral("invalid_path");
        case persistence::LastKnownGoodError::io:
            return QStringLiteral("io_error");
    }
    return QStringLiteral("unknown_error");
}
} // namespace

AiActivityModel::AiActivityModel(QString filePath, QObject *parent)
    : QAbstractListModel(parent), m_filePath(std::move(filePath))
{
    m_writerPool.setMaxThreadCount(1);
    m_writerPool.setExpiryTimeout(-1);
    m_records = load(m_filePath, m_persistenceError);
}

AiActivityModel::~AiActivityModel()
{
    m_writerPool.waitForDone();
}

int AiActivityModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_records.size());
}

QVariant AiActivityModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }
    const auto &record = m_records.at(static_cast<std::size_t>(index.row()));
    switch (role)
    {
        case ActivityIdRole:
            return record.activityId;
        case TimestampRole:
            return record.timestampUtc;
        case ConversationReferenceRole:
            return record.conversationReference;
        case ToolCallReferenceRole:
            return record.toolCallReference;
        case ToolNameRole:
            return record.toolName;
        case StateRole:
            return record.state;
        case ResultCodeRole:
            return record.resultCode;
        case PermissionModeRole:
            return record.permissionMode;
        case SessionGenerationRole:
            return QVariant::fromValue<qulonglong>(record.sessionGeneration);
        case SideEffectingRole:
            return record.sideEffecting;
        case HighRiskRole:
            return record.highRisk;
        default:
            return {};
    }
}

QHash<int, QByteArray> AiActivityModel::roleNames() const
{
    return {{ActivityIdRole, "activityId"},
            {TimestampRole, "timestamp"},
            {ConversationReferenceRole, "conversationReference"},
            {ToolCallReferenceRole, "toolCallReference"},
            {ToolNameRole, "toolName"},
            {StateRole, "state"},
            {ResultCodeRole, "resultCode"},
            {PermissionModeRole, "permissionMode"},
            {SessionGenerationRole, "sessionGeneration"},
            {SideEffectingRole, "sideEffecting"},
            {HighRiskRole, "highRisk"}};
}

QString AiActivityModel::persistenceError() const
{
    return m_persistenceError;
}

QString AiActivityModel::filePath() const
{
    return m_filePath;
}

void AiActivityModel::record(const AiActivityEvent &event)
{
    if (event.conversationId.isEmpty() || event.toolCallId.isEmpty() || !validToken(event.toolName, 48)
        || !validToken(event.state, 32) || !validToken(event.resultCode, 48) || !validToken(event.permissionMode, 32))
    {
        return;
    }
    const QString conversationReference = referenceFor(QStringLiteral("conversation"), event.conversationId);
    const QString toolCallReference =
        referenceFor(event.conversationId, event.toolCallId + QLatin1Char(':') + event.toolName);
    const auto existing = std::ranges::find_if(m_records, [&](const Record &record) {
        return record.conversationReference == conversationReference && record.toolCallReference == toolCallReference;
    });
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (existing != m_records.end())
    {
        const int row = static_cast<int>(std::distance(m_records.begin(), existing));
        existing->timestampUtc = timestamp;
        existing->state = event.state;
        existing->resultCode = event.resultCode;
        existing->permissionMode = event.permissionMode;
        existing->sessionGeneration = event.sessionGeneration;
        existing->sideEffecting = event.sideEffecting;
        existing->highRisk = event.highRisk;
        emit dataChanged(index(row), index(row));
    }
    else
    {
        const int row = rowCount();
        beginInsertRows({}, row, row);
        m_records.push_back(Record{
            .activityId = referenceFor(QStringLiteral("activity"), event.conversationId + event.toolCallId + timestamp),
            .timestampUtc = timestamp,
            .conversationReference = conversationReference,
            .toolCallReference = toolCallReference,
            .toolName = event.toolName,
            .state = event.state,
            .resultCode = event.resultCode,
            .permissionMode = event.permissionMode,
            .sessionGeneration = event.sessionGeneration,
            .sideEffecting = event.sideEffecting,
            .highRisk = event.highRisk});
        endInsertRows();
        emit countChanged();
        if (m_records.size() > maximumRecords)
        {
            beginRemoveRows({}, 0, 0);
            m_records.erase(m_records.begin());
            endRemoveRows();
            emit countChanged();
        }
    }
    scheduleSave();
}

void AiActivityModel::clear()
{
    if (!m_records.empty())
    {
        beginResetModel();
        m_records.clear();
        endResetModel();
        emit countChanged();
    }
    scheduleSave();
}

bool AiActivityModel::exportTo(const QString &destinationPath) const
{
    const QString normalized = destinationPath.trimmed();
    if (normalized.isEmpty())
    {
        return false;
    }
    return save(normalized, m_records);
}

QString AiActivityModel::referenceFor(const QString &scope, const QString &value)
{
    const QByteArray digest =
        QCryptographicHash::hash((scope + QLatin1Char('\0') + value).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest.left(12));
}

bool AiActivityModel::validToken(const QString &value, const qsizetype maximumLength)
{
    if (value.isEmpty() || value.size() > maximumLength)
    {
        return false;
    }
    return std::ranges::all_of(value, [](const QChar character) {
        return character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-');
    });
}

std::vector<AiActivityModel::Record> AiActivityModel::load(const QString &filePath, QString &error)
{
    error.clear();
    const auto loaded = persistence::loadLastKnownGood(filePath, maximumDocumentBytes, validatePayload);
    if (!loaded)
    {
        error = loadErrorText(loaded.error());
        return {};
    }
    if (!loaded->has_value())
    {
        return {};
    }
    const auto array = QJsonDocument::fromJson((*loaded)->bytes).object().value(QStringLiteral("records")).toArray();
    std::vector<Record> records;
    records.reserve(std::min<std::size_t>(maximumRecords, static_cast<std::size_t>(array.size())));
    const qsizetype first = std::max<qsizetype>(0, array.size() - static_cast<qsizetype>(maximumRecords));
    for (qsizetype index = first; index < array.size(); ++index)
    {
        const auto object = array.at(index).toObject();
        Record record{.activityId = object.value(QStringLiteral("id")).toString(),
                      .timestampUtc = object.value(QStringLiteral("timestampUtc")).toString(),
                      .conversationReference = object.value(QStringLiteral("conversationRef")).toString(),
                      .toolCallReference = object.value(QStringLiteral("callRef")).toString(),
                      .toolName = object.value(QStringLiteral("tool")).toString(),
                      .state = object.value(QStringLiteral("state")).toString(),
                      .resultCode = object.value(QStringLiteral("resultCode")).toString(),
                      .permissionMode = object.value(QStringLiteral("permission")).toString(),
                      .sessionGeneration =
                          static_cast<std::uint64_t>(object.value(QStringLiteral("sessionGeneration")).toDouble()),
                      .sideEffecting = object.value(QStringLiteral("sideEffecting")).toBool(),
                      .highRisk = object.value(QStringLiteral("highRisk")).toBool()};
        if (validToken(record.activityId, 24) && validToken(record.conversationReference, 24)
            && validToken(record.toolCallReference, 24) && validToken(record.toolName, 48)
            && validToken(record.state, 32) && validToken(record.resultCode, 48)
            && validToken(record.permissionMode, 32) && !record.timestampUtc.isEmpty())
        {
            records.push_back(std::move(record));
        }
    }
    return records;
}

bool AiActivityModel::save(const QString &filePath, const std::vector<Record> &records)
{
    QJsonArray values;
    for (const auto &record : records)
    {
        values.push_back(
            QJsonObject{{QStringLiteral("id"), record.activityId},
                        {QStringLiteral("timestampUtc"), record.timestampUtc},
                        {QStringLiteral("conversationRef"), record.conversationReference},
                        {QStringLiteral("callRef"), record.toolCallReference},
                        {QStringLiteral("tool"), record.toolName},
                        {QStringLiteral("state"), record.state},
                        {QStringLiteral("resultCode"), record.resultCode},
                        {QStringLiteral("permission"), record.permissionMode},
                        {QStringLiteral("sessionGeneration"), static_cast<double>(record.sessionGeneration)},
                        {QStringLiteral("sideEffecting"), record.sideEffecting},
                        {QStringLiteral("highRisk"), record.highRisk}});
    }
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), currentSchemaVersion},
                                                         {QStringLiteral("records"), values}})
                                   .toJson(QJsonDocument::Compact);
    return persistence::saveLastKnownGood(filePath, payload, maximumDocumentBytes, validatePayload).has_value();
}

void AiActivityModel::scheduleSave()
{
    const QString filePath = m_filePath;
    const auto records = std::make_shared<const std::vector<Record>>(m_records);
    m_writerPool.start([filePath, records]() noexcept {
        try
        {
            static_cast<void>(save(filePath, *records));
        }
        catch (...)
        {
            qWarning("AI activity persistence suppressed an exception");
        }
    });
}

} // namespace ztermy::ai
