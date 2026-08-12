#include "infrastructure/ai/McpJsonRpcProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>
#include <limits>

namespace ztermy::ai
{
namespace
{
constexpr qsizetype maximumBufferedBytes = 1024 * 1024;
constexpr qsizetype maximumMessageBytes = 512 * 1024;
constexpr std::size_t maximumTools = 128;

[[nodiscard]] QByteArray line(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
}

[[nodiscard]] std::optional<std::uint64_t> identifier(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (number < 0 || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())
        || number != std::floor(number))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}
} // namespace

QByteArray McpJsonRpcProtocol::initializeRequest(const std::uint64_t id) const
{
    return line(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
                                               {QStringLiteral("capabilities"), QJsonObject{}},
                                               {QStringLiteral("clientInfo"),
                                                QJsonObject{{QStringLiteral("name"), QStringLiteral("ztermy")},
                                                            {QStringLiteral("version"), QStringLiteral("0.3.0")}}}}}});
}

QByteArray McpJsonRpcProtocol::initializedNotification() const
{
    return line(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("method"), QStringLiteral("notifications/initialized")}});
}

QByteArray McpJsonRpcProtocol::listToolsRequest(const std::uint64_t id) const
{
    return line(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("id"), static_cast<qint64>(id)},
                            {QStringLiteral("method"), QStringLiteral("tools/list")},
                            {QStringLiteral("params"), QJsonObject{}}});
}

std::expected<QByteArray, QString> McpJsonRpcProtocol::callToolRequest(const std::uint64_t id,
                                                                       const std::string_view toolName,
                                                                       const std::string_view argumentsJson) const
{
    if (toolName.empty() || toolName.size() > 64 || argumentsJson.size() > 64 * 1024)
    {
        return std::unexpected(QStringLiteral("The MCP tool request exceeds its bounds."));
    }
    QJsonParseError error;
    const QJsonDocument arguments =
        QJsonDocument::fromJson(QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &error);
    if (error.error != QJsonParseError::NoError || !arguments.isObject())
    {
        return std::unexpected(QStringLiteral("MCP tool arguments must be a JSON object."));
    }
    return line(
        QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), static_cast<qint64>(id)},
                    {QStringLiteral("method"), QStringLiteral("tools/call")},
                    {QStringLiteral("params"), QJsonObject{{QStringLiteral("name"), QString::fromUtf8(toolName)},
                                                           {QStringLiteral("arguments"), arguments.object()}}}});
}

QByteArray McpJsonRpcProtocol::cancelRequestNotification(const std::uint64_t id, const std::string_view reason) const
{
    const QString boundedReason = QString::fromUtf8(reason.data(), static_cast<qsizetype>(reason.size())).left(256);
    return line(
        QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("method"), QStringLiteral("notifications/cancelled")},
                    {QStringLiteral("params"), QJsonObject{{QStringLiteral("requestId"), static_cast<qint64>(id)},
                                                           {QStringLiteral("reason"), boundedReason}}}});
}

std::expected<std::vector<McpJsonRpcMessage>, QString> McpJsonRpcProtocol::append(QByteArray bytes)
{
    if (bytes.size() > maximumBufferedBytes || m_buffer.size() > maximumBufferedBytes - bytes.size())
    {
        m_buffer.clear();
        return std::unexpected(QStringLiteral("The MCP JSON-RPC buffer exceeded 1 MiB."));
    }
    m_buffer.append(bytes);
    std::vector<McpJsonRpcMessage> messages;
    while (true)
    {
        const qsizetype delimiter = m_buffer.indexOf('\n');
        if (delimiter < 0)
        {
            break;
        }
        QByteArray payload = m_buffer.left(delimiter);
        m_buffer.remove(0, delimiter + 1);
        if (payload.endsWith('\r'))
        {
            payload.chop(1);
        }
        if (payload.isEmpty())
        {
            continue;
        }
        if (payload.size() > maximumMessageBytes)
        {
            return std::unexpected(QStringLiteral("An MCP JSON-RPC message exceeded 512 KiB."));
        }
        QJsonParseError error;
        const QJsonDocument parsed = QJsonDocument::fromJson(payload, &error);
        const QJsonObject object = parsed.object();
        if (error.error != QJsonParseError::NoError || !parsed.isObject()
            || object.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0"))
        {
            return std::unexpected(QStringLiteral("An MCP JSON-RPC message is invalid."));
        }
        const auto id = identifier(object.value(QStringLiteral("id")));
        const QString method = object.value(QStringLiteral("method")).toString();
        if (!id.has_value() && method.isEmpty())
        {
            return std::unexpected(QStringLiteral("An MCP message has neither a valid id nor method."));
        }
        messages.push_back(McpJsonRpcMessage{.id = id,
                                             .method = method.toUtf8().toStdString(),
                                             .result = object.value(QStringLiteral("result")).toObject(),
                                             .error = object.value(QStringLiteral("error")).toObject()});
    }
    if (m_buffer.size() > maximumMessageBytes)
    {
        m_buffer.clear();
        return std::unexpected(QStringLiteral("An incomplete MCP JSON-RPC message exceeded 512 KiB."));
    }
    return messages;
}

std::expected<std::vector<McpDiscoveredTool>, QString>
McpJsonRpcProtocol::discoveredTools(const McpJsonRpcMessage &message)
{
    const QJsonArray values = message.result.value(QStringLiteral("tools")).toArray();
    if (values.size() > static_cast<qsizetype>(maximumTools))
    {
        return std::unexpected(QStringLiteral("The MCP server exposed too many tools."));
    }
    std::vector<McpDiscoveredTool> tools;
    tools.reserve(static_cast<std::size_t>(values.size()));
    for (const QJsonValue &value : values)
    {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString description = object.value(QStringLiteral("description")).toString();
        const QJsonObject schema = object.value(QStringLiteral("inputSchema")).toObject();
        if (name.isEmpty() || schema.isEmpty())
        {
            return std::unexpected(QStringLiteral("An MCP tool definition is incomplete."));
        }
        tools.push_back(
            McpDiscoveredTool{.name = name.toUtf8().toStdString(),
                              .description = description.toUtf8().toStdString(),
                              .inputSchemaJson = QJsonDocument(schema).toJson(QJsonDocument::Compact).toStdString()});
    }
    return tools;
}

} // namespace ztermy::ai
