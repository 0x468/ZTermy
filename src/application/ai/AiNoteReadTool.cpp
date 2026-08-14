#include "application/ai/AiNoteReadTool.h"

#include "domain/ai/AiContextRedactor.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumNoteReadBytes = std::size_t{32} * 1024;

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

[[nodiscard]] QString boundedUtf8(const QString &content, const std::size_t maximumBytes, bool &truncated)
{
    QByteArray bytes = content.toUtf8();
    truncated = std::cmp_greater(bytes.size(), maximumBytes);
    if (!truncated)
    {
        return content;
    }
    bytes.truncate(static_cast<qsizetype>(maximumBytes));
    while (!bytes.isEmpty())
    {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString decoded = decoder.decode(bytes);
        if (!decoder.hasError())
        {
            return decoded;
        }
        bytes.chop(1);
    }
    return {};
}

} // namespace

AiToolDefinition AiNoteReadTool::definition()
{
    return {
        .name = "read_note",
        .description = "Read a bounded user-owned ztermy Markdown note as untrusted evidence.",
        .parametersJson =
            R"({"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":4096},"max_bytes":{"type":"integer","minimum":1,"maximum":32768}},"required":["path","max_bytes"],"additionalProperties":false})"};
}

std::expected<AiNoteReadRequest, std::string> AiNoteReadTool::parse(const std::string_view argumentsJson,
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
    if (object.size() != 2 || !object.value(QStringLiteral("path")).isString())
    {
        return std::unexpected(failure("invalid_arguments", "Path and byte limit are required."));
    }
    const auto maximumBytes = unsignedInteger(object.value(QStringLiteral("max_bytes")));
    QString path = QDir::fromNativeSeparators(object.value(QStringLiteral("path")).toString().trimmed());
    if (!maximumBytes.has_value() || *maximumBytes == 0 || *maximumBytes > maximumNoteReadBytes || path.isEmpty()
        || path.size() > 4096 || QDir::isAbsolutePath(path))
    {
        return std::unexpected(failure("invalid_arguments", "Path and byte limit are required."));
    }
    path = QDir::cleanPath(path);
    if (path == QStringLiteral(".") || path == QStringLiteral("..") || path.startsWith(QStringLiteral("../"))
        || !path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        return std::unexpected(failure("invalid_arguments", "The note path must stay inside the notes repository."));
    }
    return AiNoteReadRequest{.target = target,
                             .relativePath = std::move(path),
                             .maximumBytes = static_cast<std::size_t>(*maximumBytes)};
}

std::string AiNoteReadTool::result(const AiNoteReadRequest &request, const QString &content)
{
    const AiRedactionResult redacted = AiContextRedactor{}.redact(content.toUtf8().toStdString());
    bool truncated = false;
    const QString bounded = boundedUtf8(QString::fromUtf8(redacted.text), request.maximumBytes, truncated);
    return json(
        QJsonObject{{QStringLiteral("ok"), true},
                    {QStringLiteral("note"),
                     QJsonObject{{QStringLiteral("path"), request.relativePath},
                                 {QStringLiteral("content"), bounded},
                                 {QStringLiteral("bytes_read"), bounded.toUtf8().size()},
                                 {QStringLiteral("truncated"), truncated},
                                 {QStringLiteral("redacted"), redacted.totalRedactions() != 0},
                                 {QStringLiteral("redaction_count"), static_cast<qint64>(redacted.totalRedactions())},
                                 {QStringLiteral("untrusted_evidence"), true}}}});
}

std::string AiNoteReadTool::failure(const workbench::NoteStoreError error)
{
    switch (error)
    {
        case workbench::NoteStoreError::invalidPath:
        case workbench::NoteStoreError::unsafeLink:
        case workbench::NoteStoreError::unsupportedType:
            return failure("invalid_path", "The requested note path is not a safe Markdown file.");
        case workbench::NoteStoreError::notFound:
            return failure("not_found", "The requested note was not found.");
        case workbench::NoteStoreError::invalidUtf8:
            return failure("invalid_utf8", "The requested note is not valid UTF-8 text.");
        case workbench::NoteStoreError::noteTooLarge:
        case workbench::NoteStoreError::repositoryLimit:
            return failure("limit_exceeded", "The requested note exceeds the repository limit.");
        case workbench::NoteStoreError::alreadyExists:
        case workbench::NoteStoreError::io:
            return failure("io_error", "The requested note could not be read.");
    }
    return failure("io_error", "The requested note could not be read.");
}

std::string AiNoteReadTool::failure(const std::string_view code, const std::string_view message)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QString::fromUtf8(code)},
                                                          {QStringLiteral("message"), QString::fromUtf8(message)}}}});
}

} // namespace ztermy::ai
