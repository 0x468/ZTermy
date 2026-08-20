#include "application/ai/AiNativeToolCatalog.h"

#include <QtTest/QTest>

#include <algorithm>
#include <array>
#include <expected>
#include <string_view>
#include <vector>

namespace
{

using ztermy::ai::AiNativeToolCapabilities;
using ztermy::ai::AiNativeToolCatalog;
using ztermy::ai::AiTerminalReadSnapshot;
using ztermy::terminal::CommandBlock;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] bool contains(const std::vector<ztermy::ai::AiToolDefinition> &definitions, const std::string_view name)
{
    return std::ranges::find(definitions, name, &ztermy::ai::AiToolDefinition::name) != definitions.end();
}

[[nodiscard]] AiTerminalReadSnapshot fullSnapshot()
{
    AiTerminalReadSnapshot snapshot{
        .title = "Current terminal",
        .host = "host",
        .shell = "bash",
        .terminalFrame = "prompt",
        .capability = TerminalSemanticCapability::rich,
        .connected = true,
        .commandBlocks = {CommandBlock{.id = 7, .state = CommandBlockState::finished}},
    };
    snapshot.operations.sftpListingAvailable = true;
    snapshot.operations.shellHistory = {{.command = "pwd", .shell = "bash"}};
    snapshot.operations.scripts = {{.id = "inspect", .name = "Inspect"}};
    snapshot.operations.notes = {{.path = "ops.md", .name = "ops.md"}};
    snapshot.operations.portForwarding = {{.id = "web", .profileName = "Current terminal"}};
    snapshot.operations.telemetry = {.state = "ready", .osName = "Linux"};
    snapshot.commandOutputReader = [](const auto, const auto, const auto) {
        return std::expected<ztermy::terminal::CommandOutputArtifactPage, ztermy::terminal::CommandOutputArtifactError>{
            ztermy::terminal::CommandOutputArtifactPage{}};
    };
    return snapshot;
}

class AiNativeToolCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void hidesUnavailableCurrentTerminalCapabilities();
    void exposesOnlyActionableCurrentTerminalCapabilities();
};

void AiNativeToolCatalogTests::hidesUnavailableCurrentTerminalCapabilities()
{
    const auto definitions =
        AiNativeToolCatalog::build(AiTerminalReadSnapshot{}, AiNativeToolCapabilities{.actionsAllowed = true});
    constexpr std::array expected{std::string_view{"read_terminal_info"}, std::string_view{"read_terminal"},
                                  std::string_view{"save_runbook"}};
    QCOMPARE(definitions.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        QCOMPARE(definitions[index].name, expected[index]);
        QVERIFY(!definitions[index].name.contains("session"));
    }
    QVERIFY(!contains(definitions, "run_command"));
    QVERIFY(!contains(definitions, "read_sftp_file"));
    QVERIFY(!contains(definitions, "read_remote_telemetry"));
    QVERIFY(!contains(definitions, "list_port_forwarding"));
}

void AiNativeToolCatalogTests::exposesOnlyActionableCurrentTerminalCapabilities()
{
    const auto definitions =
        AiNativeToolCatalog::build(fullSnapshot(), AiNativeToolCapabilities{.terminalBufferAvailable = true,
                                                                            .terminalFrameAvailable = true,
                                                                            .actionsAllowed = true,
                                                                            .terminalWriteAvailable = true,
                                                                            .commandWaitAvailable = true,
                                                                            .sftpBrowserAvailable = true,
                                                                            .sftpTransferAvailable = true,
                                                                            .remoteTelemetryAvailable = true});
    constexpr std::array expected{
        std::string_view{"read_terminal_info"},   std::string_view{"read_terminal"},
        std::string_view{"read_command_block"},   std::string_view{"read_command_output"},
        std::string_view{"list_sftp_directory"},  std::string_view{"list_shell_history"},
        std::string_view{"list_scripts"},         std::string_view{"read_script"},
        std::string_view{"list_notes"},           std::string_view{"read_remote_telemetry"},
        std::string_view{"list_port_forwarding"}, std::string_view{"read_terminal_output"},
        std::string_view{"read_terminal_frame"},  std::string_view{"wait_terminal_frame"},
        std::string_view{"wait_command"},         std::string_view{"read_sftp_file"},
        std::string_view{"list_sftp_path"},       std::string_view{"read_note"},
        std::string_view{"run_command"},          std::string_view{"interrupt_command"},
        std::string_view{"write_to_pty"},         std::string_view{"save_runbook"},
        std::string_view{"queue_sftp_download"},  std::string_view{"queue_sftp_upload"},
    };
    QCOMPARE(definitions.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        QCOMPARE(definitions[index].name, expected[index]);
        QVERIFY(!definitions[index].name.contains("session"));
        QVERIFY(!definitions[index].parametersJson.contains("session_id"));
        QVERIFY(!definitions[index].parametersJson.contains("session_generation"));
    }
    QVERIFY(!contains(definitions, "list_sessions"));
}

} // namespace

QTEST_GUILESS_MAIN(AiNativeToolCatalogTests)

#include "ai_native_tool_catalog_tests.moc"
