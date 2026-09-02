#include "infrastructure/logging/ConnectionHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <optional>
#include <ranges>

namespace
{

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QJsonObject serialize(const ztermy::logging::ConnectionHistoryEntry &entry)
{
    return {{QStringLiteral("id"), text(entry.id)},
            {QStringLiteral("sessionId"), text(entry.sessionId)},
            {QStringLiteral("profileId"), text(entry.profileId)},
            {QStringLiteral("hostLabel"), text(entry.hostLabel)},
            {QStringLiteral("hostname"), text(entry.hostname)},
            {QStringLiteral("username"), text(entry.username)},
            {QStringLiteral("protocol"), text(entry.protocol)},
            {QStringLiteral("localUsername"), text(entry.localUsername)},
            {QStringLiteral("localHostname"), text(entry.localHostname)},
            {QStringLiteral("status"), text(entry.status)},
            {QStringLiteral("phase"), text(entry.phase)},
            {QStringLiteral("failure"), text(entry.failure)},
            {QStringLiteral("rawLogPath"), text(entry.rawLogPath)},
            {QStringLiteral("startedUtcMs"), entry.startedUtcMs},
            {QStringLiteral("endedUtcMs"), entry.endedUtcMs},
            {QStringLiteral("saved"), entry.saved}};
}

[[nodiscard]] std::optional<ztermy::logging::ConnectionHistoryEntry> parse(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const auto string = [&object](const char *key) -> std::optional<std::string> {
        const QJsonValue value = object.value(QLatin1String(key));
        return value.isString() ? std::optional{text(value.toString())} : std::nullopt;
    };
    const auto id = string("id");
    const auto sessionId = string("sessionId");
    const auto profileId = string("profileId");
    const auto hostLabel = string("hostLabel");
    const auto hostname = string("hostname");
    const auto username = string("username");
    const auto protocol = string("protocol");
    const auto localUsername = string("localUsername");
    const auto localHostname = string("localHostname");
    const auto status = string("status");
    const auto phase = string("phase");
    const auto failure = string("failure");
    const auto rawLogPath = string("rawLogPath");
    const QJsonValue started = object.value(QStringLiteral("startedUtcMs"));
    const QJsonValue ended = object.value(QStringLiteral("endedUtcMs"));
    const QJsonValue saved = object.value(QStringLiteral("saved"));
    if (!id || !sessionId || !profileId || !hostLabel || !hostname || !username || !protocol || !localUsername
        || !localHostname || !status || !phase || !failure || !rawLogPath || !started.isDouble() || !ended.isDouble()
        || !saved.isBool())
    {
        return std::nullopt;
    }
    ztermy::logging::ConnectionHistoryEntry entry{
        .id = *id,
        .sessionId = *sessionId,
        .profileId = *profileId,
        .hostLabel = *hostLabel,
        .hostname = *hostname,
        .username = *username,
        .protocol = *protocol,
        .localUsername = *localUsername,
        .localHostname = *localHostname,
        .status = *status,
        .phase = *phase,
        .failure = *failure,
        .rawLogPath = *rawLogPath,
        .startedUtcMs = started.toInteger(-1),
        .endedUtcMs = ended.toInteger(-1),
        .saved = saved.toBool(),
    };
    return ztermy::logging::validConnectionHistoryEntry(entry) ? std::optional{std::move(entry)} : std::nullopt;
}

} // namespace

namespace ztermy::logging
{

ConnectionHistoryStore::ConnectionHistoryStore(QString path) : m_path(std::move(path)) {}

std::expected<ConnectionHistory, ConnectionHistoryStoreError> ConnectionHistoryStore::load() const
{
    QFile file(m_path);
    if (!file.exists())
    {
        return ConnectionHistory{};
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(ConnectionHistoryStoreError::Io);
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(ConnectionHistoryStoreError::Corrupt);
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt(-1) != 1)
    {
        return std::unexpected(ConnectionHistoryStoreError::UnsupportedVersion);
    }
    const QJsonValue values = root.value(QStringLiteral("entries"));
    if (!values.isArray())
    {
        return std::unexpected(ConnectionHistoryStoreError::Corrupt);
    }
    ConnectionHistory history;
    history.entries.reserve(static_cast<std::size_t>(values.toArray().size()));
    for (const QJsonValue value : values.toArray())
    {
        auto entry = parse(value);
        if (!entry)
        {
            return std::unexpected(ConnectionHistoryStoreError::Corrupt);
        }
        history.entries.push_back(std::move(*entry));
    }
    pruneConnectionHistory(history);
    return history;
}

std::expected<void, ConnectionHistoryStoreError> ConnectionHistoryStore::save(const ConnectionHistory &history) const
{
    if (std::ranges::any_of(history.entries, [](const ConnectionHistoryEntry &entry) {
            return !validConnectionHistoryEntry(entry);
        }))
    {
        return std::unexpected(ConnectionHistoryStoreError::Corrupt);
    }
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.absolutePath()))
    {
        return std::unexpected(ConnectionHistoryStoreError::Io);
    }
    QJsonArray entries;
    for (const ConnectionHistoryEntry &entry : history.entries)
    {
        entries.push_back(serialize(entry));
    }
    QSaveFile file(m_path);
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("entries"), entries}})
            .toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        return std::unexpected(ConnectionHistoryStoreError::Io);
    }
    return {};
}

} // namespace ztermy::logging
