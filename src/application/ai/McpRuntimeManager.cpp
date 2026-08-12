#include "application/ai/McpRuntimeManager.h"

#include <QFileInfo>

#include <algorithm>
#include <ranges>

namespace ztermy::ai
{

McpRuntimeManager::McpRuntimeManager(QString storePath, ChangedHandler changedHandler)
    : m_store(std::move(storePath)), m_changedHandler(std::move(changedHandler))
{
}

McpRuntimeManager::~McpRuntimeManager()
{
    shutdown();
}

void McpRuntimeManager::initialize()
{
    if (m_initialized)
    {
        return;
    }
    m_initialized = true;
    auto loaded = m_store.load();
    if (!loaded.has_value())
    {
        m_operationError = loaded.error();
        publishChanged();
        return;
    }
    m_records = std::move(*loaded);
    if (m_store.lastLoadRecoveredFromBackup())
    {
        m_operationError =
            QStringLiteral("The MCP server configuration was recovered from its last known good backup.");
    }
    for (auto &item : m_records)
    {
        start(item);
    }
    publishChanged();
}

void McpRuntimeManager::shutdown()
{
    for (auto &item : m_runtimes)
    {
        if (item->client)
        {
            item->client->stop();
        }
    }
    m_runtimes.clear();
}

std::vector<McpServerSnapshot> McpRuntimeManager::servers() const
{
    std::vector<McpServerSnapshot> result;
    result.reserve(m_records.size());
    for (const auto &item : m_records)
    {
        const Runtime *active = runtime(item.configuration.identity.id);
        result.push_back(McpServerSnapshot{.record = item,
                                           .state = active == nullptr ? QStringLiteral("stopped") : active->state,
                                           .error = active == nullptr ? QString{} : active->error});
    }
    return result;
}

std::vector<McpToolSnapshot> McpRuntimeManager::tools() const
{
    std::vector<McpToolSnapshot> result;
    for (const auto &active : m_runtimes)
    {
        for (const auto &tool : active->tools)
        {
            const McpServerRecord *saved = record(tool.serverId);
            const bool approved =
                saved != nullptr && std::ranges::any_of(saved->approvedTools, [&tool](const auto &item) {
                    return item.exposedName == tool.exposedName && item.schemaDigest == tool.schemaDigest;
                });
            result.push_back(McpToolSnapshot{.tool = tool, .approved = approved});
        }
    }
    return result;
}

std::vector<AiToolDefinition> McpRuntimeManager::definitions() const
{
    return m_registry.definitions();
}

std::optional<McpRegisteredTool> McpRuntimeManager::resolve(const std::string_view exposedName) const
{
    return m_registry.resolve(exposedName);
}

QString McpRuntimeManager::operationError() const
{
    return m_operationError;
}

bool McpRuntimeManager::saveServer(McpServerRecord candidate)
{
    auto records = m_records;
    const auto existing = std::ranges::find(records, candidate.configuration.identity.id, [](const auto &item) {
        return item.configuration.identity.id;
    });
    const bool newServer = existing == records.end();
    if (newServer)
    {
        records.push_back(std::move(candidate));
    }
    else
    {
        candidate.approvedTools = existing->approvedTools;
        *existing = std::move(candidate);
    }
    if (!persist(records))
    {
        return false;
    }
    const std::string id = newServer ? records.back().configuration.identity.id : existing->configuration.identity.id;
    if (Runtime *old = runtime(id); old != nullptr && old->client)
    {
        old->client->stop();
    }
    m_runtimes.erase(std::remove_if(m_runtimes.begin(), m_runtimes.end(),
                                    [&id](const auto &item) {
                                        return item->serverId == id;
                                    }),
                     m_runtimes.end());
    m_records = std::move(records);
    if (McpServerRecord *saved = record(id); saved != nullptr)
    {
        start(*saved);
    }
    m_operationError.clear();
    publishChanged();
    return true;
}

bool McpRuntimeManager::removeServer(const std::string_view serverId)
{
    auto records = m_records;
    const auto before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [serverId](const auto &item) {
                                     return item.configuration.identity.id == serverId;
                                 }),
                  records.end());
    if (records.size() == before || !persist(records))
    {
        return false;
    }
    if (Runtime *old = runtime(serverId); old != nullptr && old->client)
    {
        old->client->stop();
    }
    m_registry.disableServer(serverId);
    m_runtimes.erase(std::remove_if(m_runtimes.begin(), m_runtimes.end(),
                                    [serverId](const auto &item) {
                                        return item->serverId == serverId;
                                    }),
                     m_runtimes.end());
    m_records = std::move(records);
    m_operationError.clear();
    publishChanged();
    return true;
}

bool McpRuntimeManager::restartServer(const std::string_view serverId)
{
    McpServerRecord *saved = record(serverId);
    if (saved == nullptr)
    {
        return false;
    }
    if (Runtime *old = runtime(serverId); old != nullptr && old->client)
    {
        old->client->stop();
    }
    m_runtimes.erase(std::remove_if(m_runtimes.begin(), m_runtimes.end(),
                                    [serverId](const auto &item) {
                                        return item->serverId == serverId;
                                    }),
                     m_runtimes.end());
    start(*saved);
    publishChanged();
    return true;
}

bool McpRuntimeManager::setToolApproved(const std::string_view serverId, const std::string_view exposedName,
                                        const std::string_view schemaDigest, const bool approved)
{
    McpServerRecord *saved = record(serverId);
    Runtime *active = runtime(serverId);
    if (saved == nullptr || active == nullptr)
    {
        return false;
    }
    const auto discovered = std::ranges::find_if(active->tools, [=](const auto &tool) {
        return tool.serverId == serverId && tool.exposedName == exposedName && tool.schemaDigest == schemaDigest;
    });
    if (discovered == active->tools.end())
    {
        return false;
    }
    auto records = m_records;
    auto candidate = std::ranges::find(records, std::string(serverId), [](const auto &item) {
        return item.configuration.identity.id;
    });
    if (candidate == records.end())
    {
        return false;
    }
    candidate->approvedTools.erase(std::remove_if(candidate->approvedTools.begin(), candidate->approvedTools.end(),
                                                  [=](const auto &item) {
                                                      return item.exposedName == exposedName;
                                                  }),
                                   candidate->approvedTools.end());
    if (approved)
    {
        candidate->approvedTools.push_back(
            {.exposedName = std::string(exposedName), .schemaDigest = std::string(schemaDigest)});
    }
    if (!persist(records))
    {
        return false;
    }
    const bool registryChanged =
        approved ? m_registry.approve(serverId, exposedName, schemaDigest) : m_registry.revoke(serverId, exposedName);
    if (!registryChanged)
    {
        m_operationError = QStringLiteral("The MCP tool approval no longer matches the discovered schema.");
        publishChanged();
        return false;
    }
    m_records = std::move(records);
    m_operationError.clear();
    publishChanged();
    return true;
}

std::expected<McpCallHandle, QString> McpRuntimeManager::call(const std::string_view exposedName,
                                                              const std::string_view argumentsJson, CallHandler handler)
{
    const auto tool = m_registry.resolve(exposedName);
    if (!tool.has_value())
    {
        return std::unexpected(QStringLiteral("The MCP tool is no longer approved."));
    }
    Runtime *active = runtime(tool->serverId);
    if (active == nullptr || !active->client || !active->client->ready())
    {
        return std::unexpected(QStringLiteral("The MCP server is not ready."));
    }
    auto request = active->client->call(exposedName, argumentsJson, std::move(handler));
    if (!request.has_value())
    {
        return std::unexpected(request.error());
    }
    return McpCallHandle{.serverId = tool->serverId, .requestId = *request};
}

bool McpRuntimeManager::cancel(const McpCallHandle &handle, const std::string_view reason)
{
    Runtime *active = runtime(handle.serverId);
    return active != nullptr && active->client && active->client->cancel(handle.requestId, reason);
}

McpServerRecord *McpRuntimeManager::record(const std::string_view serverId)
{
    const auto found = std::ranges::find(m_records, serverId, [](const auto &item) {
        return std::string_view(item.configuration.identity.id);
    });
    return found == m_records.end() ? nullptr : &*found;
}

const McpServerRecord *McpRuntimeManager::record(const std::string_view serverId) const
{
    const auto found = std::ranges::find(m_records, serverId, [](const auto &item) {
        return std::string_view(item.configuration.identity.id);
    });
    return found == m_records.end() ? nullptr : &*found;
}

McpRuntimeManager::Runtime *McpRuntimeManager::runtime(const std::string_view serverId)
{
    const auto found = std::ranges::find(m_runtimes, serverId, [](const auto &item) {
        return std::string_view(item->serverId);
    });
    return found == m_runtimes.end() ? nullptr : found->get();
}

const McpRuntimeManager::Runtime *McpRuntimeManager::runtime(const std::string_view serverId) const
{
    const auto found = std::ranges::find(m_runtimes, serverId, [](const auto &item) {
        return std::string_view(item->serverId);
    });
    return found == m_runtimes.end() ? nullptr : found->get();
}

void McpRuntimeManager::start(McpServerRecord &saved)
{
    auto active = std::make_unique<Runtime>();
    active->serverId = saved.configuration.identity.id;
    if (!saved.enabled || saved.configuration.identity.trust == McpServerTrust::disabled)
    {
        active->state = QStringLiteral("disabled");
        m_registry.disableServer(active->serverId);
        m_runtimes.push_back(std::move(active));
        return;
    }
    const QFileInfo executable(saved.configuration.program);
    const QFileInfo workingDirectory(saved.configuration.workingDirectory);
    if (!executable.isAbsolute() || !executable.isFile() || !executable.isExecutable()
        || (!saved.configuration.workingDirectory.isEmpty()
            && (!workingDirectory.isAbsolute() || !workingDirectory.isDir())))
    {
        active->state = QStringLiteral("error");
        active->error = QStringLiteral("Use an existing absolute executable and working directory for MCP stdio.");
        m_runtimes.push_back(std::move(active));
        return;
    }
    active->client = std::make_unique<McpStdioClient>(m_registry);
    active->state = QStringLiteral("starting");
    Runtime *runtimePointer = active.get();
    const std::string serverId = active->serverId;
    auto started = active->client->start(saved.configuration, [this, serverId, runtimePointer](auto discovered) {
        if (runtime(serverId) != runtimePointer)
        {
            return;
        }
        if (!discovered.has_value())
        {
            runtimePointer->state = QStringLiteral("error");
            runtimePointer->error = discovered.error();
            publishChanged();
            return;
        }
        runtimePointer->tools = discovered->tools;
        runtimePointer->state = QStringLiteral("ready");
        runtimePointer->error.clear();
        const McpServerRecord *current = record(serverId);
        if (current != nullptr)
        {
            for (const auto &approval : current->approvedTools)
            {
                static_cast<void>(m_registry.approve(serverId, approval.exposedName, approval.schemaDigest));
            }
        }
        publishChanged();
    });
    if (!started.has_value())
    {
        active->state = QStringLiteral("error");
        active->error = started.error();
        active->client.reset();
    }
    m_runtimes.push_back(std::move(active));
}

void McpRuntimeManager::publishChanged()
{
    if (m_changedHandler)
    {
        m_changedHandler();
    }
}

bool McpRuntimeManager::persist(const std::vector<McpServerRecord> &records)
{
    const auto saved = m_store.save(records);
    if (!saved.has_value())
    {
        m_operationError = saved.error();
        publishChanged();
        return false;
    }
    return true;
}

} // namespace ztermy::ai
