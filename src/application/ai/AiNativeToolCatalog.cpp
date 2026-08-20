#include "application/ai/AiNativeToolCatalog.h"

#include "application/ai/AiActionToolDispatcher.h"
#include "application/ai/AiNoteReadTool.h"
#include "application/ai/AiReadToolDispatcher.h"
#include "application/ai/AiSftpListTool.h"
#include "application/ai/AiSftpReadTool.h"
#include "application/ai/AiTerminalFrameTool.h"
#include "application/ai/AiTerminalOutputTool.h"
#include "application/ai/AiWaitCommandTool.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

namespace ztermy::ai
{
namespace
{

void appendNamed(std::vector<AiToolDefinition> &target, std::vector<AiToolDefinition> definitions,
                 const std::span<const std::string_view> names)
{
    for (auto &definition : definitions)
    {
        if (std::ranges::find(names, definition.name) != names.end())
        {
            target.push_back(std::move(definition));
        }
    }
}

} // namespace

std::vector<AiToolDefinition> AiNativeToolCatalog::build(const AiTerminalReadSnapshot &terminal,
                                                         const AiNativeToolCapabilities capabilities)
{
    std::vector<std::string_view> readNames{"read_terminal_info", "read_terminal"};
    if (!terminal.commandBlocks.empty())
    {
        readNames.emplace_back("read_command_block");
    }
    if (terminal.commandOutputReader)
    {
        readNames.emplace_back("read_command_output");
    }
    if (terminal.operations.sftpListingAvailable)
    {
        readNames.emplace_back("list_sftp_directory");
    }
    if (!terminal.operations.shellHistory.empty())
    {
        readNames.emplace_back("list_shell_history");
    }
    if (!terminal.operations.scripts.empty())
    {
        readNames.emplace_back("list_scripts");
        readNames.emplace_back("read_script");
    }
    if (!terminal.operations.notes.empty())
    {
        readNames.emplace_back("list_notes");
    }
    if (capabilities.remoteTelemetryAvailable)
    {
        readNames.emplace_back("read_remote_telemetry");
    }
    if (!terminal.operations.portForwarding.empty())
    {
        readNames.emplace_back("list_port_forwarding");
    }

    std::vector<AiToolDefinition> result;
    result.reserve(readNames.size() + 10);
    appendNamed(result, AiReadToolDispatcher::definitions(), readNames);

    if (capabilities.terminalBufferAvailable)
    {
        result.push_back(AiTerminalOutputTool::definition());
    }
    if (capabilities.terminalFrameAvailable)
    {
        result.push_back(AiTerminalFrameTool::readDefinition());
        result.push_back(AiTerminalFrameTool::waitDefinition());
    }
    if (capabilities.commandWaitAvailable)
    {
        result.push_back(AiWaitCommandTool::definition());
    }
    if (capabilities.sftpBrowserAvailable)
    {
        result.push_back(AiSftpReadTool::definition());
        result.push_back(AiSftpListTool::definition());
    }
    if (!terminal.operations.notes.empty())
    {
        result.push_back(AiNoteReadTool::definition());
    }

    if (capabilities.actionsAllowed)
    {
        std::vector<std::string_view> actionNames{"save_runbook"};
        if (capabilities.terminalWriteAvailable)
        {
            actionNames.emplace_back("run_command");
            actionNames.emplace_back("interrupt_command");
            actionNames.emplace_back("write_to_pty");
        }
        if (capabilities.sftpTransferAvailable)
        {
            actionNames.emplace_back("queue_sftp_download");
            actionNames.emplace_back("queue_sftp_upload");
        }
        appendNamed(result, AiActionToolDispatcher::definitions(), actionNames);
    }

    return result;
}

} // namespace ztermy::ai
