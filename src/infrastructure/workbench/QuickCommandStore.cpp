#include "infrastructure/workbench/QuickCommandStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace
{

constexpr qint64 currentSchemaVersion = 1;
constexpr qint64 maximumFileSize = qint64{8} * 1024 * 1024;
constexpr qsizetype maximumQuickCommandCount = 1000;

[[nodiscard]] std::optional<ztermy::workbench::ShellScope> parseShellScope(const QString &value)
{
    if (value == QStringLiteral("any"))
    {
        return ztermy::workbench::ShellScope::any;
    }
    if (value == QStringLiteral("posix"))
    {
        return ztermy::workbench::ShellScope::posix;
    }
    if (value == QStringLiteral("powershell"))
    {
        return ztermy::workbench::ShellScope::powershell;
    }
    return std::nullopt;
}

[[nodiscard]] QString shellScopeToken(const ztermy::workbench::ShellScope shellScope)
{
    switch (shellScope)
    {
        case ztermy::workbench::ShellScope::posix:
            return QStringLiteral("posix");
        case ztermy::workbench::ShellScope::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellScope::any:
        default:
            return QStringLiteral("any");
    }
}

[[nodiscard]] std::optional<std::int64_t> parseTimestamp(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble(-1.0);
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
}

[[nodiscard]] std::optional<ztermy::workbench::QuickCommand> parseQuickCommand(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    const QJsonValue nameValue = object.value(QStringLiteral("name"));
    const QJsonValue commandValue = object.value(QStringLiteral("command"));
    const QJsonValue descriptionValue = object.value(QStringLiteral("description"));
    const QJsonValue shellValue = object.value(QStringLiteral("shell"));
    if (!idValue.isString() || !nameValue.isString() || !commandValue.isString() || !descriptionValue.isString()
        || !shellValue.isString())
    {
        return std::nullopt;
    }

    const auto shellScope = parseShellScope(shellValue.toString());
    const auto createdUtcMs = parseTimestamp(object.value(QStringLiteral("createdUtcMs")));
    const auto modifiedUtcMs = parseTimestamp(object.value(QStringLiteral("modifiedUtcMs")));
    if (!shellScope || !createdUtcMs || !modifiedUtcMs)
    {
        return std::nullopt;
    }

    ztermy::workbench::QuickCommand quickCommand{
        .id = idValue.toString().toStdString(),
        .name = nameValue.toString().toStdString(),
        .command = commandValue.toString().toStdString(),
        .description = descriptionValue.toString().toStdString(),
        .shellScope = *shellScope,
        .createdUtcMs = *createdUtcMs,
        .modifiedUtcMs = *modifiedUtcMs,
    };
    return ztermy::workbench::validQuickCommand(quickCommand) ? std::optional{std::move(quickCommand)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeQuickCommand(const ztermy::workbench::QuickCommand &quickCommand)
{
    return {
        {QStringLiteral("id"), QString::fromStdString(quickCommand.id)},
        {QStringLiteral("name"), QString::fromStdString(quickCommand.name)},
        {QStringLiteral("command"), QString::fromStdString(quickCommand.command)},
        {QStringLiteral("description"), QString::fromStdString(quickCommand.description)},
        {QStringLiteral("shell"), shellScopeToken(quickCommand.shellScope)},
        {QStringLiteral("createdUtcMs"), quickCommand.createdUtcMs},
        {QStringLiteral("modifiedUtcMs"), quickCommand.modifiedUtcMs},
    };
}

[[nodiscard]] bool containsDuplicateIds(const std::span<const ztermy::workbench::QuickCommand> quickCommands)
{
    for (std::size_t index = 0; index < quickCommands.size(); ++index)
    {
        const auto duplicate =
            std::find_if(quickCommands.begin() + static_cast<std::ptrdiff_t>(index + 1), quickCommands.end(),
                         [&quickCommands, index](const ztermy::workbench::QuickCommand &candidate) {
                             return candidate.id == quickCommands[index].id;
                         });
        if (duplicate != quickCommands.end())
        {
            return true;
        }
    }
    return false;
}

} // namespace

namespace ztermy::workbench
{

QuickCommandStore::QuickCommandStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &QuickCommandStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<QuickCommand>, QuickCommandStoreError> QuickCommandStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(QuickCommandStoreError::invalidPath);
    }

    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<QuickCommand>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumFileSize)
    {
        return std::unexpected(file.size() > maximumFileSize ? QuickCommandStoreError::invalidFormat
                                                             : QuickCommandStoreError::ioError);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QuickCommandStoreError::invalidFormat);
    }

    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble() || versionValue.toInteger(-1) != currentSchemaVersion
        || versionValue.toDouble() != static_cast<double>(currentSchemaVersion))
    {
        return std::unexpected(versionValue.isDouble() ? QuickCommandStoreError::unsupportedVersion
                                                       : QuickCommandStoreError::invalidFormat);
    }
    const QJsonValue commandValues = root.value(QStringLiteral("commands"));
    if (!commandValues.isArray() || commandValues.toArray().size() > maximumQuickCommandCount)
    {
        return std::unexpected(QuickCommandStoreError::invalidFormat);
    }

    std::vector<QuickCommand> quickCommands;
    quickCommands.reserve(static_cast<std::size_t>(commandValues.toArray().size()));
    for (const QJsonValue value : commandValues.toArray())
    {
        auto quickCommand = parseQuickCommand(value);
        if (!quickCommand)
        {
            return std::unexpected(QuickCommandStoreError::invalidFormat);
        }
        quickCommands.push_back(std::move(*quickCommand));
    }
    if (containsDuplicateIds(quickCommands))
    {
        return std::unexpected(QuickCommandStoreError::invalidFormat);
    }
    return quickCommands;
}

std::expected<void, QuickCommandStoreError>
QuickCommandStore::save(const std::span<const QuickCommand> quickCommands) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(QuickCommandStoreError::invalidPath);
    }
    if (quickCommands.size() > static_cast<std::size_t>(maximumQuickCommandCount) || containsDuplicateIds(quickCommands)
        || std::ranges::any_of(quickCommands, [](const QuickCommand &quickCommand) {
               return !validQuickCommand(quickCommand);
           }))
    {
        return std::unexpected(QuickCommandStoreError::invalidFormat);
    }

    QJsonArray commandValues;
    for (const QuickCommand &quickCommand : quickCommands)
    {
        commandValues.append(serializeQuickCommand(quickCommand));
    }
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), currentSchemaVersion},
        {QStringLiteral("commands"), commandValues},
    });
    const QByteArray data = document.toJson(QJsonDocument::Indented);
    if (data.size() > maximumFileSize)
    {
        return std::unexpected(QuickCommandStoreError::invalidFormat);
    }

    const QFileInfo fileInfo(m_filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(QuickCommandStoreError::ioError);
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(QuickCommandStoreError::ioError);
    }
    return {};
}

} // namespace ztermy::workbench
