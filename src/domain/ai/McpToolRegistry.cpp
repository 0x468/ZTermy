#include "domain/ai/McpToolRegistry.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cctype>
#include <ranges>

namespace ztermy::ai
{
namespace
{
constexpr std::size_t maximumToolsPerServer = 128;
constexpr std::size_t maximumDescriptionBytes = 4096;
constexpr std::size_t maximumSchemaBytes = 64 * 1024;

[[nodiscard]] bool validToken(const std::string_view value)
{
    return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

[[nodiscard]] std::string digest(const std::string_view value)
{
    return QCryptographicHash::hash(QByteArray(value.data(), static_cast<qsizetype>(value.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

[[nodiscard]] bool validSchema(const std::string_view value)
{
    if (value.empty() || value.size() > maximumSchemaBytes)
    {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(value.data(), static_cast<qsizetype>(value.size())), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
           && document.object().value(QStringLiteral("type")).toString() == QStringLiteral("object");
}
} // namespace

std::expected<McpDiscoveryUpdate, std::string>
McpToolRegistry::update(const McpServerIdentity &server, const std::span<const McpDiscoveredTool> discovered)
{
    if (!validToken(server.id) || !validToken(server.nameSpace) || discovered.size() > maximumToolsPerServer)
    {
        return std::unexpected("The MCP server identity or tool count is invalid.");
    }
    std::vector<McpRegisteredTool> next;
    next.reserve(discovered.size());
    for (const auto &tool : discovered)
    {
        if (!validToken(tool.name) || tool.description.size() > maximumDescriptionBytes
            || !validSchema(tool.inputSchemaJson))
        {
            return std::unexpected("An MCP tool name, description, or input schema is invalid.");
        }
        const std::string exposed = "mcp__" + server.nameSpace + "__" + tool.name;
        if (std::ranges::any_of(next, [&exposed](const auto &candidate) {
                return candidate.exposedName == exposed;
            }))
        {
            return std::unexpected("MCP tool names must be unique inside a server namespace.");
        }
        const std::string schemaDigest = digest(tool.inputSchemaJson);
        const auto existing = std::ranges::find_if(m_tools, [&server, &exposed](const auto &candidate) {
            return candidate.serverId == server.id && candidate.exposedName == exposed;
        });
        const bool approved = existing != m_tools.end() && existing->schemaDigest == schemaDigest
                              && existing->description == tool.description && existing->schemaApproved;
        next.push_back(McpRegisteredTool{.serverId = server.id,
                                         .serverNamespace = server.nameSpace,
                                         .remoteName = tool.name,
                                         .exposedName = exposed,
                                         .description = tool.description,
                                         .inputSchemaJson = tool.inputSchemaJson,
                                         .schemaDigest = schemaDigest,
                                         .trust = server.trust,
                                         .schemaApproved = approved});
    }
    m_tools.erase(std::remove_if(m_tools.begin(), m_tools.end(),
                                 [&server](const auto &candidate) {
                                     return candidate.serverId == server.id;
                                 }),
                  m_tools.end());
    m_tools.insert(m_tools.end(), next.begin(), next.end());
    return McpDiscoveryUpdate{.tools = std::move(next),
                              .reviewRequired = std::ranges::any_of(m_tools, [&server](const auto &tool) {
                                  return tool.serverId == server.id && !tool.schemaApproved;
                              })};
}

bool McpToolRegistry::approve(const std::string_view serverId, const std::string_view exposedName,
                              const std::string_view schemaDigest)
{
    const auto found = std::ranges::find_if(m_tools, [=](const auto &tool) {
        return tool.serverId == serverId && tool.exposedName == exposedName && tool.schemaDigest == schemaDigest;
    });
    if (found == m_tools.end() || found->trust == McpServerTrust::disabled)
    {
        return false;
    }
    found->schemaApproved = true;
    return true;
}

bool McpToolRegistry::revoke(const std::string_view serverId, const std::string_view exposedName)
{
    const auto found = std::ranges::find_if(m_tools, [=](const auto &tool) {
        return tool.serverId == serverId && tool.exposedName == exposedName;
    });
    if (found == m_tools.end())
    {
        return false;
    }
    found->schemaApproved = false;
    return true;
}

std::vector<AiToolDefinition> McpToolRegistry::definitions() const
{
    std::vector<AiToolDefinition> result;
    for (const auto &tool : m_tools)
    {
        if (tool.trust == McpServerTrust::execute && tool.schemaApproved)
        {
            result.push_back(AiToolDefinition{.name = tool.exposedName,
                                              .description = "Untrusted MCP tool description: " + tool.description,
                                              .parametersJson = tool.inputSchemaJson});
        }
    }
    return result;
}

std::optional<McpRegisteredTool> McpToolRegistry::resolve(const std::string_view exposedName) const
{
    const auto found = std::ranges::find(m_tools, exposedName, &McpRegisteredTool::exposedName);
    return found != m_tools.end() && found->trust == McpServerTrust::execute && found->schemaApproved
               ? std::optional<McpRegisteredTool>{*found}
               : std::nullopt;
}

void McpToolRegistry::disableServer(const std::string_view serverId)
{
    for (auto &tool : m_tools)
    {
        if (tool.serverId == serverId)
        {
            tool.trust = McpServerTrust::disabled;
        }
    }
}

} // namespace ztermy::ai
