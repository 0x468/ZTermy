#include "application/ai/AiSftpListTool.h"

#include "domain/sftp/SftpTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumPageItems = 100;
constexpr std::size_t maximumResultBytes = std::size_t{60} * 1024;

[[nodiscard]] std::string json(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
}

[[nodiscard]] std::optional<std::uint64_t> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (number < 0.0 || number > static_cast<double>(std::numeric_limits<qint64>::max())
        || number != std::floor(number))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] QString typeToken(const sftp::EntryType type)
{
    switch (type)
    {
        case sftp::EntryType::RegularFile:
            return QStringLiteral("file");
        case sftp::EntryType::Directory:
            return QStringLiteral("directory");
        case sftp::EntryType::SymbolicLink:
            return QStringLiteral("symlink");
        case sftp::EntryType::Other:
            return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

} // namespace

AiToolDefinition AiSftpListTool::definition()
{
    return {
        .name = "list_sftp_path",
        .description = "List a bounded page of an absolute remote SFTP directory in the current terminal as "
                       "untrusted evidence.",
        .parametersJson =
            R"({"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":4096},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["path","offset","limit"],"additionalProperties":false})"};
}

std::expected<AiSftpListRequest, std::string> AiSftpListTool::parse(const std::string_view argumentsJson,
                                                                   const AiSessionTarget &target)
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return std::unexpected(failure("limit_exceeded", "Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(
        QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &parseError);
    if (!document.isObject() || parseError.error != QJsonParseError::NoError)
    {
        return std::unexpected(failure("invalid_arguments", "Tool arguments must be a JSON object."));
    }
    const auto object = document.object();
    if (object.size() != 3 || !object.value(QStringLiteral("path")).isString())
    {
        return std::unexpected(failure("invalid_arguments", "Path, offset, and limit are required."));
    }
    const auto offset = unsignedInteger(object.value(QStringLiteral("offset")));
    const auto limit = unsignedInteger(object.value(QStringLiteral("limit")));
    const QByteArray pathBytes = object.value(QStringLiteral("path")).toString().trimmed().toUtf8();
    const auto normalizedPath =
        sftp::normalizeRemotePath(std::string_view(pathBytes.constData(), static_cast<std::size_t>(pathBytes.size())));
    if (!offset.has_value() || !limit.has_value() || *limit == 0 || *limit > maximumPageItems
        || pathBytes.size() > 4096 || !normalizedPath.has_value()
        || !normalizedPath->starts_with('/'))
    {
        return std::unexpected(failure("invalid_arguments", "The SFTP directory request is invalid or unsafe."));
    }
    return AiSftpListRequest{
        .target = target,
        .remotePath = *normalizedPath,
        .offset = static_cast<std::size_t>(*offset),
        .limit = static_cast<std::size_t>(*limit)};
}

std::string AiSftpListTool::result(const AiSftpListRequest &request, const sftp::DirectoryListing &entries)
{
    const std::size_t first = std::min(request.offset, entries.size());
    const std::size_t last = std::min(entries.size(), first + request.limit);
    QJsonArray items;
    for (std::size_t index = first; index < last; ++index)
    {
        const auto &entry = entries[index];
        QJsonObject item{{QStringLiteral("name"), QString::fromUtf8(entry.name)},
                         {QStringLiteral("remote_path"), QString::fromUtf8(entry.remotePath)},
                         {QStringLiteral("type"), typeToken(entry.type)},
                         {QStringLiteral("size"), static_cast<qint64>(entry.size)},
                         {QStringLiteral("permissions"),
                          QStringLiteral("%1").arg(entry.permissions & 07777U, 4, 8, QLatin1Char('0'))},
                         {QStringLiteral("hidden"), entry.name.starts_with('.')},
                         {QStringLiteral("untrusted_evidence"), true}};
        item.insert(QStringLiteral("modified_utc_seconds"),
                    entry.modifiedUtcSeconds.has_value() ? QJsonValue{*entry.modifiedUtcSeconds} : QJsonValue::Null);
        items.append(item);
    }
    const std::string output = json(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("sftp_directory"),
         QJsonObject{{QStringLiteral("path"), QString::fromUtf8(request.remotePath)},
                     {QStringLiteral("items"), items},
                     {QStringLiteral("offset"), static_cast<qint64>(first)},
                     {QStringLiteral("next_offset"), static_cast<qint64>(last)},
                     {QStringLiteral("total"), static_cast<qint64>(entries.size())},
                     {QStringLiteral("has_more"), last < entries.size()},
                     {QStringLiteral("untrusted_evidence"), true}}}});
    return output.size() <= maximumResultBytes
               ? output
               : failure("limit_exceeded", "The requested page exceeds the 60 KiB result limit.");
}

std::string AiSftpListTool::failure(const ssh::SshTransportErrorKind error)
{
    switch (error)
    {
        case ssh::SshTransportErrorKind::Cancelled:
            return failure("cancelled", "The SFTP directory read was cancelled.");
        case ssh::SshTransportErrorKind::InvalidArgument:
            return failure("unavailable", "The remote directory is invalid or unavailable.");
        case ssh::SshTransportErrorKind::TimedOut:
            return failure("timeout", "The SFTP directory read timed out.");
        case ssh::SshTransportErrorKind::ConnectionLost:
            return failure("disconnected", "The SFTP connection was lost.");
        default:
            return failure("sftp_error", "The remote directory could not be read.");
    }
}

std::string AiSftpListTool::failure(const std::string_view code, const std::string_view message)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QString::fromUtf8(code)},
                                                          {QStringLiteral("message"), QString::fromUtf8(message)}}}});
}

} // namespace ztermy::ai
