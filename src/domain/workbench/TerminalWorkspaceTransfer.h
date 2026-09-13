#pragma once

#include "domain/workbench/WorkspaceState.h"

namespace ztermy::workbench
{

enum class TerminalTransferKind : std::uint8_t
{
    MovePane,
    SwapPanes,
    ExtractPane,
    MergeWorkspace,
};

struct TerminalWorkspaceTransfer final
{
    TerminalTransferKind kind = TerminalTransferKind::MovePane;
    std::string sourceWorkspaceId;
    std::string sourcePaneId;
    std::string targetWorkspaceId;
    std::string targetPaneId;
    std::string splitNodeId;
    TerminalSplitOrientation orientation = TerminalSplitOrientation::Horizontal;
    bool placeAfter = true;
};

// Publishes only a fully valid candidate. IDs and restore intents move together;
// this layer never creates, reconnects, or stops a live session.
[[nodiscard]] bool transferTerminalWorkspace(WorkspaceState &state, const TerminalWorkspaceTransfer &transfer);

} // namespace ztermy::workbench
