#pragma once

#include "domain/ai/McpToolRegistry.h"

#include <QByteArray>
#include <QJsonObject>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace ztermy::ai
{

struct McpJsonRpcMessage final
{
    std::optional<std::uint64_t> id;
    std::string method;
    QJsonObject result;
    QJsonObject error;
};

class McpJsonRpcProtocol final
{
public:
    [[nodiscard]] QByteArray initializeRequest(std::uint64_t id) const;
    [[nodiscard]] QByteArray initializedNotification() const;
    [[nodiscard]] QByteArray listToolsRequest(std::uint64_t id) const;
    [[nodiscard]] std::expected<QByteArray, QString> callToolRequest(std::uint64_t id, std::string_view toolName,
                                                                     std::string_view argumentsJson) const;
    [[nodiscard]] QByteArray cancelRequestNotification(std::uint64_t id, std::string_view reason) const;
    [[nodiscard]] std::expected<std::vector<McpJsonRpcMessage>, QString> append(const QByteArray &bytes);
    [[nodiscard]] static std::expected<std::vector<McpDiscoveredTool>, QString>
    discoveredTools(const McpJsonRpcMessage &message);

private:
    QByteArray m_buffer;
};

} // namespace ztermy::ai
