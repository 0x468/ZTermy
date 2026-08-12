#pragma once

#include "infrastructure/ai/McpServerStore.h"

#include <QString>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

struct McpServerSnapshot final
{
    McpServerRecord record;
    QString state;
    QString error;
};

struct McpToolSnapshot final
{
    McpRegisteredTool tool;
    bool approved = false;
};

struct McpCallHandle final
{
    std::string serverId;
    std::uint64_t requestId = 0;
};

class McpRuntimeManager final
{
public:
    using ChangedHandler = std::function<void()>;
    using CallHandler = McpStdioClient::CallHandler;

    explicit McpRuntimeManager(QString storePath, ChangedHandler changedHandler = {});
    ~McpRuntimeManager();

    McpRuntimeManager(const McpRuntimeManager &) = delete;
    McpRuntimeManager &operator=(const McpRuntimeManager &) = delete;

    void initialize();
    void shutdown();
    [[nodiscard]] std::vector<McpServerSnapshot> servers() const;
    [[nodiscard]] std::vector<McpToolSnapshot> tools() const;
    [[nodiscard]] std::vector<AiToolDefinition> definitions() const;
    [[nodiscard]] std::optional<McpRegisteredTool> resolve(std::string_view exposedName) const;
    [[nodiscard]] QString operationError() const;

    [[nodiscard]] bool saveServer(McpServerRecord record);
    [[nodiscard]] bool removeServer(std::string_view serverId);
    [[nodiscard]] bool restartServer(std::string_view serverId);
    [[nodiscard]] bool setToolApproved(std::string_view serverId, std::string_view exposedName,
                                       std::string_view schemaDigest, bool approved);
    [[nodiscard]] std::expected<McpCallHandle, QString> call(std::string_view exposedName,
                                                             std::string_view argumentsJson, CallHandler handler);
    [[nodiscard]] bool cancel(const McpCallHandle &handle, std::string_view reason = "Cancelled by the user.");

private:
    struct Runtime final
    {
        std::string serverId;
        std::unique_ptr<McpStdioClient> client;
        std::vector<McpRegisteredTool> tools;
        QString state = QStringLiteral("stopped");
        QString error;
    };

    [[nodiscard]] McpServerRecord *record(std::string_view serverId);
    [[nodiscard]] const McpServerRecord *record(std::string_view serverId) const;
    [[nodiscard]] Runtime *runtime(std::string_view serverId);
    [[nodiscard]] const Runtime *runtime(std::string_view serverId) const;
    void start(McpServerRecord &record);
    void publishChanged();
    [[nodiscard]] bool persist(const std::vector<McpServerRecord> &records);

    McpServerStore m_store;
    McpToolRegistry m_registry;
    std::vector<McpServerRecord> m_records;
    std::vector<std::unique_ptr<Runtime>> m_runtimes;
    ChangedHandler m_changedHandler;
    QString m_operationError;
    bool m_initialized = false;
};

} // namespace ztermy::ai
