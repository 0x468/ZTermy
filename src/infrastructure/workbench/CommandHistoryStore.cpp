#include "infrastructure/workbench/CommandHistoryStore.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace
{

constexpr qint64 maximumFileSize = qint64{16} * 1024 * 1024;

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString shellToken(const ztermy::workbench::ShellKind shell)
{
    using ztermy::workbench::ShellKind;
    switch (shell)
    {
        case ShellKind::bash:
            return QStringLiteral("bash");
        case ShellKind::zsh:
            return QStringLiteral("zsh");
        case ShellKind::fish:
            return QStringLiteral("fish");
        case ShellKind::powershell:
            return QStringLiteral("powershell");
        case ShellKind::nushell:
            return QStringLiteral("nushell");
        case ShellKind::unknown:
        default:
            return QStringLiteral("unknown");
    }
}

[[nodiscard]] std::optional<ztermy::workbench::ShellKind> parseShell(const QString &token)
{
    using ztermy::workbench::ShellKind;
    if (token == QStringLiteral("bash"))
        return ShellKind::bash;
    if (token == QStringLiteral("zsh"))
        return ShellKind::zsh;
    if (token == QStringLiteral("fish"))
        return ShellKind::fish;
    if (token == QStringLiteral("powershell"))
        return ShellKind::powershell;
    if (token == QStringLiteral("nushell"))
        return ShellKind::nushell;
    if (token == QStringLiteral("unknown"))
        return ShellKind::unknown;
    return std::nullopt;
}

[[nodiscard]] QJsonObject serialize(const ztermy::workbench::IndexedCommand &entry)
{
    return {{QStringLiteral("command"), text(entry.command)},
            {QStringLiteral("sourceId"), text(entry.sourceId)},
            {QStringLiteral("sourceLabel"), text(entry.sourceLabel)},
            {QStringLiteral("shell"), shellToken(entry.shell)},
            {QStringLiteral("firstUsedUtcSeconds"), entry.firstUsedUtcSeconds},
            {QStringLiteral("lastUsedUtcSeconds"), entry.lastUsedUtcSeconds},
            {QStringLiteral("useCount"), static_cast<qint64>(entry.useCount)}};
}

[[nodiscard]] std::optional<ztermy::workbench::IndexedCommand> parse(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    const auto shell = parseShell(object.value(QStringLiteral("shell")).toString());
    const qint64 first = object.value(QStringLiteral("firstUsedUtcSeconds")).toInteger(-1);
    const qint64 last = object.value(QStringLiteral("lastUsedUtcSeconds")).toInteger(-1);
    const qint64 useCount = object.value(QStringLiteral("useCount")).toInteger(-1);
    if (!object.value(QStringLiteral("command")).isString() || !object.value(QStringLiteral("sourceId")).isString()
        || !object.value(QStringLiteral("sourceLabel")).isString() || !shell || first < 0 || last < first
        || useCount <= 0 || std::cmp_greater(useCount, (std::numeric_limits<std::uint32_t>::max)()))
        return std::nullopt;
    ztermy::workbench::IndexedCommand entry{
        .command = text(object.value(QStringLiteral("command")).toString()),
        .sourceId = text(object.value(QStringLiteral("sourceId")).toString()),
        .sourceLabel = text(object.value(QStringLiteral("sourceLabel")).toString()),
        .shell = *shell,
        .firstUsedUtcSeconds = first,
        .lastUsedUtcSeconds = last,
        .useCount = static_cast<std::uint32_t>(useCount),
    };
    return ztermy::workbench::validIndexedCommand(entry) ? std::optional{std::move(entry)} : std::nullopt;
}

[[nodiscard]] ztermy::persistence::PayloadValidation validate(const QByteArrayView bytes)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return ztermy::persistence::PayloadValidation::invalid;
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("version")).isDouble())
        return ztermy::persistence::PayloadValidation::invalid;
    if (root.value(QStringLiteral("version")).toInt(-1) != 1)
        return ztermy::persistence::PayloadValidation::unsupportedVersion;
    const QJsonValue entries = root.value(QStringLiteral("entries"));
    if (!entries.isArray()
        || entries.toArray().size() > static_cast<qsizetype>(ztermy::workbench::maximumIndexedCommandCount)
        || std::ranges::any_of(entries.toArray(), [](const QJsonValue &value) {
               return !parse(value);
           }))
        return ztermy::persistence::PayloadValidation::invalid;
    return ztermy::persistence::PayloadValidation::valid;
}

} // namespace

namespace ztermy::workbench
{

CommandHistoryStore::CommandHistoryStore(QString path) : m_path(std::move(path)) {}

std::expected<CommandHistoryIndex, CommandHistoryStoreError> CommandHistoryStore::load() const
{
    const auto loaded = persistence::loadLastKnownGood(m_path, maximumFileSize, validate);
    if (!loaded)
        return std::unexpected(CommandHistoryStoreError::io);
    if (!loaded->has_value())
        return CommandHistoryIndex{};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(loaded->value().bytes, &error);
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != 1)
        return std::unexpected(CommandHistoryStoreError::unsupportedVersion);
    const QJsonValue values = root.value(QStringLiteral("entries"));
    if (!values.isArray())
        return std::unexpected(CommandHistoryStoreError::corrupt);
    CommandHistoryIndex result;
    result.entries.reserve(static_cast<std::size_t>(values.toArray().size()));
    for (const QJsonValue value : values.toArray())
    {
        auto entry = parse(value);
        if (!entry)
            return std::unexpected(CommandHistoryStoreError::corrupt);
        result.entries.push_back(std::move(*entry));
    }
    if (result.entries.size() > maximumIndexedCommandCount)
        result.entries.resize(maximumIndexedCommandCount);
    return result;
}

std::expected<void, CommandHistoryStoreError> CommandHistoryStore::save(const CommandHistoryIndex &index) const
{
    if (index.entries.size() > maximumIndexedCommandCount
        || std::ranges::any_of(index.entries, [](const IndexedCommand &entry) {
               return !validIndexedCommand(entry);
           }))
        return std::unexpected(CommandHistoryStoreError::corrupt);
    QJsonArray entries;
    for (const IndexedCommand &entry : index.entries)
        entries.push_back(serialize(entry));
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries}})
            .toJson(QJsonDocument::Compact);
    if (!persistence::saveLastKnownGood(m_path, bytes, maximumFileSize, validate))
        return std::unexpected(CommandHistoryStoreError::io);
    return {};
}

} // namespace ztermy::workbench
