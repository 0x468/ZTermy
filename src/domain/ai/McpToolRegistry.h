#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class McpServerTrust : std::uint8_t
{
    disabled,
    observe,
    execute
};

struct McpServerIdentity final
{
    std::string id;
    std::string nameSpace;
    McpServerTrust trust = McpServerTrust::disabled;
};

struct McpDiscoveredTool final
{
    std::string name;
    std::string description;
    std::string inputSchemaJson;
};

struct McpRegisteredTool final
{
    std::string serverId;
    std::string serverNamespace;
    std::string remoteName;
    std::string exposedName;
    std::string description;
    std::string inputSchemaJson;
    std::string schemaDigest;
    McpServerTrust trust = McpServerTrust::disabled;
    bool schemaApproved = false;
};

struct McpDiscoveryUpdate final
{
    std::vector<McpRegisteredTool> tools;
    bool reviewRequired = false;
};

class McpToolRegistry final
{
public:
    [[nodiscard]] std::expected<McpDiscoveryUpdate, std::string> update(const McpServerIdentity &server,
                                                                        std::span<const McpDiscoveredTool> discovered);
    [[nodiscard]] bool approve(std::string_view serverId, std::string_view exposedName, std::string_view schemaDigest);
    [[nodiscard]] std::vector<AiToolDefinition> definitions() const;
    [[nodiscard]] std::optional<McpRegisteredTool> resolve(std::string_view exposedName) const;
    void disableServer(std::string_view serverId);

private:
    std::vector<McpRegisteredTool> m_tools;
};

} // namespace ztermy::ai
