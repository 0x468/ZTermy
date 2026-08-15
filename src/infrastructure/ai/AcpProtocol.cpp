#include "infrastructure/ai/AcpProtocol.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>
#include <limits>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumBufferedBytes = qsizetype{4} * 1024 * 1024;
constexpr qsizetype maximumMessageBytes = qsizetype{2} * 1024 * 1024;
constexpr std::size_t maximumPromptBytes = std::size_t{1024} * 1024;
constexpr std::uint64_t maximumJsonInteger = 9'007'199'254'740'991ULL;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QByteArray framed(QJsonObject object)
{
    object.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    QByteArray result = QJsonDocument(object).toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

[[nodiscard]] bool validUtf8(const std::string_view value, const std::size_t maximumBytes, const bool allowNewlines)
{
    if (value.empty() || value.size() > maximumBytes)
    {
        return false;
    }
    const QString decoded = text(value);
    if (decoded.contains(QChar::ReplacementCharacter))
    {
        return false;
    }
    for (const QChar character : decoded)
    {
        const auto codePoint = character.unicode();
        const bool allowedWhitespace = allowNewlines && (codePoint == '\n' || codePoint == '\r' || codePoint == '\t');
        if ((!allowedWhitespace && (codePoint < 0x20U || codePoint == 0x7FU)) || codePoint == 0x85U
            || codePoint == 0x2028U || codePoint == 0x2029U)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validIdentifier(const std::string_view value, const std::size_t maximumBytes = 512)
{
    return validUtf8(value, maximumBytes, false);
}

[[nodiscard]] bool validRequestId(const std::uint64_t id) noexcept
{
    return id <= maximumJsonInteger;
}

[[nodiscard]] bool validWireId(const QJsonValue &value)
{
    if (value.isString())
    {
        const QByteArray encoded = value.toString().toUtf8();
        return validIdentifier(std::string_view(encoded.constData(), static_cast<std::size_t>(encoded.size())));
    }
    if (!value.isDouble())
    {
        return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && number >= 0.0 && std::floor(number) == number
           && number <= static_cast<double>(maximumJsonInteger);
}

[[nodiscard]] std::expected<QJsonObject, QString> sessionParameters(const std::string_view sessionId,
                                                                    const std::string_view workingDirectory)
{
    if ((!sessionId.empty() && !validIdentifier(sessionId)) || !validIdentifier(workingDirectory, 4096)
        || !QDir::isAbsolutePath(text(workingDirectory)))
    {
        return std::unexpected(QStringLiteral("ACP requires a bounded session id and absolute working directory."));
    }
    QJsonObject params{{QStringLiteral("cwd"), text(workingDirectory)}, {QStringLiteral("mcpServers"), QJsonArray{}}};
    if (!sessionId.empty())
    {
        params.insert(QStringLiteral("sessionId"), text(sessionId));
    }
    return params;
}

} // namespace

std::expected<QByteArray, QString> AcpProtocol::initializeRequest(const std::uint64_t id,
                                                                  const std::string_view clientVersion,
                                                                  const bool terminalCapability) const
{
    if (!validRequestId(id) || !validIdentifier(clientVersion, 128))
    {
        return std::unexpected(QStringLiteral("The ACP client identity is invalid."));
    }
    QJsonObject clientCapabilities;
    if (terminalCapability)
    {
        clientCapabilities.insert(QStringLiteral("terminal"), true);
    }
    return framed(QJsonObject{
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("protocolVersion"), 1},
                     {QStringLiteral("clientCapabilities"), clientCapabilities},
                     {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("ztermy")},
                                                                {QStringLiteral("title"), QStringLiteral("ztermy")},
                                                                {QStringLiteral("version"), text(clientVersion)}}}}},
    });
}

std::expected<QByteArray, QString> AcpProtocol::newSessionRequest(const std::uint64_t id,
                                                                  const std::string_view workingDirectory) const
{
    if (!validRequestId(id))
    {
        return std::unexpected(QStringLiteral("The ACP request id is invalid."));
    }
    auto params = sessionParameters({}, workingDirectory);
    if (!params.has_value())
    {
        return std::unexpected(params.error());
    }
    return framed(QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)},
                              {QStringLiteral("method"), QStringLiteral("session/new")},
                              {QStringLiteral("params"), *params}});
}

std::expected<QByteArray, QString> AcpProtocol::resumeSessionRequest(const std::uint64_t id,
                                                                     const std::string_view sessionId,
                                                                     const std::string_view workingDirectory) const
{
    if (!validRequestId(id))
    {
        return std::unexpected(QStringLiteral("The ACP request id is invalid."));
    }
    auto params = sessionParameters(sessionId, workingDirectory);
    if (!params.has_value())
    {
        return std::unexpected(params.error());
    }
    return framed(QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)},
                              {QStringLiteral("method"), QStringLiteral("session/resume")},
                              {QStringLiteral("params"), *params}});
}

std::expected<QByteArray, QString> AcpProtocol::promptRequest(const std::uint64_t id, const std::string_view sessionId,
                                                              const std::string_view prompt) const
{
    if (!validRequestId(id) || !validIdentifier(sessionId) || !validUtf8(prompt, maximumPromptBytes, true))
    {
        return std::unexpected(QStringLiteral("The ACP prompt request is invalid or too large."));
    }
    const QJsonArray blocks{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text(prompt)}}};
    return framed(QJsonObject{
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("method"), QStringLiteral("session/prompt")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("sessionId"), text(sessionId)}, {QStringLiteral("prompt"), blocks}}},
    });
}

std::expected<QByteArray, QString> AcpProtocol::cancelNotification(const std::string_view sessionId) const
{
    if (!validIdentifier(sessionId))
    {
        return std::unexpected(QStringLiteral("A valid ACP session id is required."));
    }
    return framed(QJsonObject{{QStringLiteral("method"), QStringLiteral("session/cancel")},
                              {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), text(sessionId)}}}});
}

std::expected<QByteArray, QString> AcpProtocol::closeSessionRequest(const std::uint64_t id,
                                                                    const std::string_view sessionId) const
{
    if (!validRequestId(id) || !validIdentifier(sessionId))
    {
        return std::unexpected(QStringLiteral("A valid ACP request and session id are required."));
    }
    return framed(QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)},
                              {QStringLiteral("method"), QStringLiteral("session/close")},
                              {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), text(sessionId)}}}});
}

std::expected<QByteArray, QString> AcpProtocol::resultResponse(const QJsonValue &id, const QJsonValue &result) const
{
    if (!validWireId(id) || result.isUndefined())
    {
        return std::unexpected(QStringLiteral("The ACP response is invalid."));
    }
    return framed(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

std::expected<QByteArray, QString> AcpProtocol::errorResponse(const QJsonValue &id, const int code,
                                                              const std::string_view message) const
{
    if (!validWireId(id) || !validUtf8(message, 4096, true))
    {
        return std::unexpected(QStringLiteral("The ACP error response is invalid."));
    }
    return framed(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), text(message)}}},
    });
}

std::expected<std::vector<AcpMessage>, QString> AcpProtocol::append(const QByteArray &bytes)
{
    if (bytes.size() > maximumBufferedBytes || m_buffer.size() > maximumBufferedBytes - bytes.size())
    {
        reset();
        return std::unexpected(QStringLiteral("The ACP message buffer exceeded 4 MiB."));
    }
    m_buffer.append(bytes);

    std::vector<AcpMessage> messages;
    while (true)
    {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0)
        {
            break;
        }
        QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty())
        {
            continue;
        }
        if (line.size() > maximumMessageBytes)
        {
            reset();
            return std::unexpected(QStringLiteral("An ACP message exceeded 2 MiB."));
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            reset();
            return std::unexpected(QStringLiteral("The ACP Agent emitted invalid JSON."));
        }
        const QJsonObject object = document.object();
        const bool validVersion = object.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0");
        const bool hasMethod = object.value(QStringLiteral("method")).isString();
        const bool hasParams = object.contains(QStringLiteral("params"));
        const bool hasId = object.contains(QStringLiteral("id"));
        const bool hasResult = object.contains(QStringLiteral("result"));
        const bool hasError = object.value(QStringLiteral("error")).isObject();
        const QString method = object.value(QStringLiteral("method")).toString();
        if (!validVersion || (hasId && !validWireId(object.value(QStringLiteral("id"))))
            || (hasParams && !object.value(QStringLiteral("params")).isObject()) || (hasMethod && method.isEmpty())
            || (!hasMethod && (!hasId || hasResult == hasError)) || (hasMethod && (hasResult || hasError)))
        {
            reset();
            return std::unexpected(QStringLiteral("The ACP Agent emitted an invalid JSON-RPC envelope."));
        }

        AcpMessage message;
        message.hasId = hasId;
        message.id = object.value(QStringLiteral("id"));
        message.method = method;
        message.params = object.value(QStringLiteral("params")).toObject();
        message.result = object.value(QStringLiteral("result"));
        message.error = object.value(QStringLiteral("error")).toObject();
        message.kind =
            hasMethod ? (hasId ? AcpMessageKind::request : AcpMessageKind::notification) : AcpMessageKind::response;
        messages.push_back(std::move(message));
    }
    if (m_buffer.size() > maximumMessageBytes)
    {
        reset();
        return std::unexpected(QStringLiteral("An ACP message exceeded 2 MiB."));
    }
    return messages;
}

void AcpProtocol::reset() noexcept
{
    m_buffer.clear();
}

} // namespace ztermy::ai
