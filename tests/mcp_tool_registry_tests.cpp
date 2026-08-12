#include "domain/ai/McpToolRegistry.h"

#include <QtTest/QTest>

#include <array>

namespace
{
using namespace ztermy::ai;

class McpToolRegistryTests final : public QObject
{
    Q_OBJECT

private slots:
    void requiresReviewAndIsolatesNamespaces();
    void invalidatesApprovalWhenDescriptionsOrSchemasChange();
};

void McpToolRegistryTests::requiresReviewAndIsolatesNamespaces()
{
    McpToolRegistry registry;
    const McpServerIdentity server{.id = "local-files", .nameSpace = "files", .trust = McpServerTrust::execute};
    const std::array tools{McpDiscoveredTool{.name = "read",
                                             .description = "Read a bounded file",
                                             .inputSchemaJson = R"({"type":"object"})"}};
    auto update = registry.update(server, tools);
    QVERIFY(update.has_value());
    QVERIFY(update->reviewRequired);
    QCOMPARE(update->tools.front().exposedName, std::string("mcp__files__read"));
    QVERIFY(registry.definitions().empty());
    QVERIFY(registry.approve(server.id, update->tools.front().exposedName, update->tools.front().schemaDigest));
    QCOMPARE(registry.definitions().size(), std::size_t{1});
    registry.disableServer(server.id);
    QVERIFY(registry.definitions().empty());
}

void McpToolRegistryTests::invalidatesApprovalWhenDescriptionsOrSchemasChange()
{
    McpToolRegistry registry;
    const McpServerIdentity server{.id = "ops", .nameSpace = "ops", .trust = McpServerTrust::execute};
    const std::array firstTools{
        McpDiscoveredTool{.name = "status", .description = "Read status", .inputSchemaJson = R"({"type":"object"})"}};
    auto first = registry.update(server, firstTools);
    QVERIFY(first.has_value());
    QVERIFY(registry.approve(server.id, first->tools.front().exposedName, first->tools.front().schemaDigest));
    const std::array changedTools{McpDiscoveredTool{.name = "status",
                                                    .description = "Read status and instructions",
                                                    .inputSchemaJson = R"({"type":"object"})"}};
    auto changed = registry.update(server, changedTools);
    QVERIFY(changed.has_value());
    QVERIFY(changed->reviewRequired);
    QVERIFY(registry.definitions().empty());
}
} // namespace

QTEST_GUILESS_MAIN(McpToolRegistryTests)

#include "mcp_tool_registry_tests.moc"
