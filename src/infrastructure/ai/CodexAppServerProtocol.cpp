#include "infrastructure/ai/CodexAppServerProtocol.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <string>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumBufferedBytes = qsizetype{4} * 1024 * 1024;
constexpr qsizetype maximumMessageBytes = qsizetype{2} * 1024 * 1024;
constexpr std::size_t maximumTools = 128;
constexpr std::size_t maximumToolDescriptionBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumToolSchemaBytes = std::size_t{64} * 1024;
constexpr std::size_t maximumPromptBytes = std::size_t{1024} * 1024;
constexpr std::size_t maximumToolOutputBytes = std::size_t{256} * 1024;
constexpr std::uint64_t maximumJsonInteger = 9'007'199'254'740'991ULL;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QByteArray framed(const QJsonObject &object)
{
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

[[nodiscard]] bool validIdentifier(const std::string_view value, const std::size_t maximumBytes = 256)
{
    return validUtf8(value, maximumBytes, false);
}

[[nodiscard]] bool validRequestId(const std::uint64_t id) noexcept
{
    return id <= maximumJsonInteger;
}

[[nodiscard]] bool validToolName(const std::string_view value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return pattern.match(text(value)).hasMatch();
}

[[nodiscard]] std::expected<QJsonObject, QString> objectFromJson(const std::string_view value)
{
    if (value.empty() || value.size() > maximumToolSchemaBytes)
    {
        return std::unexpected(QStringLiteral("A dynamic tool schema must be a bounded JSON object."));
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(value.data(), static_cast<qsizetype>(value.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QStringLiteral("A dynamic tool schema is not a valid JSON object."));
    }
    return document.object();
}

[[nodiscard]] std::optional<std::uint64_t> messageId(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(maximumJsonInteger))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] std::expected<QJsonObject, QString> threadConfiguration(const std::string_view model,
                                                                      const std::string_view localWorkingDirectory,
                                                                      const std::string_view developerInstructions,
                                                                      const std::span<const AiToolDefinition> tools)
{
    if ((!model.empty() && !validIdentifier(model)) || !validIdentifier(localWorkingDirectory, 4096)
        || !QDir::isAbsolutePath(text(localWorkingDirectory))
        || !validUtf8(developerInstructions, maximumPromptBytes, true) || tools.empty() || tools.size() > maximumTools)
    {
        return std::unexpected(QStringLiteral("The Codex thread configuration exceeds its bounds."));
    }

    QJsonArray dynamicTools;
    for (const AiToolDefinition &tool : tools)
    {
        if (!validToolName(tool.name) || !validUtf8(tool.description, maximumToolDescriptionBytes, true))
        {
            return std::unexpected(QStringLiteral("A dynamic tool definition is invalid."));
        }
        const auto schema = objectFromJson(tool.parametersJson);
        if (!schema.has_value())
        {
            return std::unexpected(schema.error());
        }
        dynamicTools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                        {QStringLiteral("name"), text(tool.name)},
                                        {QStringLiteral("description"), text(tool.description)},
                                        {QStringLiteral("inputSchema"), *schema}});
    }

    QJsonObject result{{QStringLiteral("cwd"), text(localWorkingDirectory)},
                       {QStringLiteral("approvalPolicy"), QStringLiteral("never")},
                       {QStringLiteral("sandbox"), QStringLiteral("read-only")},
                       {QStringLiteral("serviceName"), QStringLiteral("ztermy")},
                       {QStringLiteral("developerInstructions"), text(developerInstructions)},
                       {QStringLiteral("dynamicTools"), dynamicTools}};
    if (!model.empty())
    {
        result.insert(QStringLiteral("model"), text(model));
    }
    return result;
}

} // namespace

QByteArray CodexAppServerProtocol::initializeRequest(const std::uint64_t id, const std::string_view clientVersion) const
{
    Q_ASSERT(validRequestId(id));
    Q_ASSERT(validIdentifier(clientVersion));
    return framed(QJsonObject{
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("ztermy")},
                                                                {QStringLiteral("title"), QStringLiteral("ztermy")},
                                                                {QStringLiteral("version"), text(clientVersion)}}},
                     {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("experimentalApi"), true}}}}},
    });
}

QByteArray CodexAppServerProtocol::initializedNotification() const
{
    return framed(QJsonObject{{QStringLiteral("method"), QStringLiteral("initialized")},
                              {QStringLiteral("params"), QJsonObject{}}});
}

std::expected<QByteArray, QString> CodexAppServerProtocol::startThreadRequest(
    const std::uint64_t id, const std::string_view model, const std::string_view localWorkingDirectory,
    const std::string_view developerInstructions, const std::span<const AiToolDefinition> tools) const
{
    if (!validRequestId(id))
    {
        return std::unexpected(QStringLiteral("The Codex thread configuration exceeds its bounds."));
    }
    const auto params = threadConfiguration(model, localWorkingDirectory, developerInstructions, tools);
    if (!params.has_value())
    {
        return std::unexpected(params.error());
    }
    return framed(QJsonObject{{QStringLiteral("method"), QStringLiteral("thread/start")},
                              {QStringLiteral("id"), static_cast<qint64>(id)},
                              {QStringLiteral("params"), *params}});
}

std::expected<QByteArray, QString>
CodexAppServerProtocol::resumeThreadRequest(const std::uint64_t id, const std::string_view threadId,
                                            const std::string_view model, const std::string_view localWorkingDirectory,
                                            const std::string_view developerInstructions,
                                            const std::span<const AiToolDefinition> tools) const
{
    if (!validRequestId(id) || !validIdentifier(threadId))
    {
        return std::unexpected(QStringLiteral("A valid Codex thread id is required."));
    }
    auto params = threadConfiguration(model, localWorkingDirectory, developerInstructions, tools);
    if (!params.has_value())
    {
        return std::unexpected(params.error());
    }
    params->insert(QStringLiteral("threadId"), text(threadId));
    return framed(QJsonObject{{QStringLiteral("method"), QStringLiteral("thread/resume")},
                              {QStringLiteral("id"), static_cast<qint64>(id)},
                              {QStringLiteral("params"), *params}});
}

std::expected<QByteArray, QString> CodexAppServerProtocol::startTurnRequest(const std::uint64_t id,
                                                                            const std::string_view threadId,
                                                                            const std::string_view prompt) const
{
    if (!validRequestId(id) || !validIdentifier(threadId) || !validUtf8(prompt, maximumPromptBytes, true))
    {
        return std::unexpected(QStringLiteral("The Codex turn request is invalid or too large."));
    }
    const QJsonArray input{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text(prompt)}}};
    return framed(QJsonObject{
        {QStringLiteral("method"), QStringLiteral("turn/start")},
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("threadId"), text(threadId)}, {QStringLiteral("input"), input}}},
    });
}

std::expected<QByteArray, QString> CodexAppServerProtocol::interruptTurnRequest(const std::uint64_t id,
                                                                                const std::string_view threadId,
                                                                                const std::string_view turnId) const
{
    if (!validRequestId(id) || !validIdentifier(threadId) || !validIdentifier(turnId))
    {
        return std::unexpected(QStringLiteral("Valid Codex thread and turn ids are required."));
    }
    return framed(QJsonObject{
        {QStringLiteral("method"), QStringLiteral("turn/interrupt")},
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("threadId"), text(threadId)}, {QStringLiteral("turnId"), text(turnId)}}},
    });
}

std::expected<QByteArray, QString> CodexAppServerProtocol::dynamicToolResponse(const std::uint64_t id,
                                                                               const bool success,
                                                                               const std::string_view output) const
{
    if (!validRequestId(id) || (output.size() > maximumToolOutputBytes)
        || (!output.empty() && !validUtf8(output, maximumToolOutputBytes, true)))
    {
        return std::unexpected(QStringLiteral("The dynamic tool result exceeds the 256 KiB limit."));
    }
    const QJsonArray contentItems{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("inputText")}, {QStringLiteral("text"), text(output)}}};
    return framed(QJsonObject{
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("result"),
         QJsonObject{{QStringLiteral("contentItems"), contentItems}, {QStringLiteral("success"), success}}},
    });
}

std::expected<std::vector<CodexAppServerMessage>, QString> CodexAppServerProtocol::append(const QByteArray &bytes)
{
    if (bytes.size() > maximumBufferedBytes || m_buffer.size() > maximumBufferedBytes - bytes.size())
    {
        reset();
        return std::unexpected(QStringLiteral("The Codex app-server message buffer exceeded 4 MiB."));
    }
    m_buffer.append(bytes);

    std::vector<CodexAppServerMessage> messages;
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
            return std::unexpected(QStringLiteral("A Codex app-server message exceeded 2 MiB."));
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            reset();
            return std::unexpected(QStringLiteral("The Codex app-server emitted invalid JSON."));
        }
        const QJsonObject object = document.object();
        const bool hasMethod = object.value(QStringLiteral("method")).isString();
        const bool hasParams = object.contains(QStringLiteral("params"));
        const bool hasId = object.contains(QStringLiteral("id"));
        const bool hasResult = object.value(QStringLiteral("result")).isObject();
        const bool hasError = object.value(QStringLiteral("error")).isObject();
        const auto id = hasId ? messageId(object.value(QStringLiteral("id"))) : std::nullopt;
        const QString method = object.value(QStringLiteral("method")).toString();
        if ((hasId && !id.has_value()) || (hasParams && !object.value(QStringLiteral("params")).isObject())
            || (hasMethod && method.isEmpty()) || (!hasMethod && hasResult == hasError) || (hasMethod && hasResult)
            || (hasMethod && hasError))
        {
            reset();
            return std::unexpected(QStringLiteral("The Codex app-server emitted an invalid message envelope."));
        }

        CodexAppServerMessage message;
        message.id = id;
        message.method = method;
        message.params = object.value(QStringLiteral("params")).toObject();
        message.result = object.value(QStringLiteral("result")).toObject();
        message.error = object.value(QStringLiteral("error")).toObject();
        message.kind = hasMethod
                           ? (hasId ? CodexAppServerMessageKind::request : CodexAppServerMessageKind::notification)
                           : CodexAppServerMessageKind::response;
        messages.push_back(std::move(message));
    }
    if (m_buffer.size() > maximumMessageBytes)
    {
        reset();
        return std::unexpected(QStringLiteral("A Codex app-server message exceeded 2 MiB."));
    }
    return messages;
}

void CodexAppServerProtocol::reset() noexcept
{
    m_buffer.clear();
}

} // namespace ztermy::ai
