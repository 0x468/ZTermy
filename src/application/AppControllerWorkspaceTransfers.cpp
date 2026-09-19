#include "application/AppController.h"
#include "ui/terminal/TerminalItem.h"

#include <QUuid>

#include <algorithm>

namespace ztermy
{
void AppController::emitActiveTerminalContextChanged(const bool refreshViewports)
{
    updateTelemetryVisibility();
    emit activeTerminalTabChanged();
    emit activeTerminalTabPinnedChanged();
    emit terminalWorkspaceChanged();
    emit remoteTelemetryChanged();
    emit sshActiveChanged();
    emit terminalSearchChanged();
    emit terminalHistoryChanged();
    emit sftpChanged();
    emit aiConversationChanged();
    if (refreshViewports)
        showActiveTab();
}

bool AppController::moveTerminalTab(const QString &id, const int targetIndex)
{
    const auto *session = findTerminalWorkspace(id) ? nullptr : findTab(id);
    const QString workspaceId = session ? session->workspaceId : id;
    const auto *source = findTerminalWorkspace(workspaceId);
    if (!source || targetIndex < 0)
        return false;
    auto candidate = m_workspaceState;
    std::vector<workbench::TerminalWorkspaceLayout> ordered;
    for (const auto &layout : candidate.terminalWorkspaces)
        if (layout.windowId == source->windowId)
            ordered.push_back(layout);
    if (static_cast<std::size_t>(targetIndex) >= ordered.size())
        return false;
    const auto position = std::ranges::find(ordered, source->id, &workbench::TerminalWorkspaceLayout::id);
    const auto destination = ordered.begin() + targetIndex;
    if (position == destination)
        return true;
    if (position < destination)
        std::rotate(position, std::next(position), std::next(destination));
    else
        std::rotate(destination, position, std::next(position));
    auto next = ordered.begin();
    for (auto &layout : candidate.terminalWorkspaces)
        if (layout.windowId == source->windowId)
            layout = std::move(*next++);
    if (!saveWorkspaceStateCandidate(candidate))
        return false;
    m_workspaceState = std::move(candidate);
    emit terminalTabsChanged();
    return true;
}

namespace
{
std::string newLayoutId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
} // namespace

bool AppController::applyTerminalTransfer(const workbench::TerminalWorkspaceTransfer &transfer)
{
    if (!terminalWorkspaceCanTransfer(QString::fromStdString(transfer.sourceWorkspaceId))
        || !terminalWorkspaceCanTransfer(QString::fromStdString(transfer.targetWorkspaceId)))
        return false;
    auto candidate = m_workspaceState;
    if (!workbench::transferTerminalWorkspace(candidate, transfer))
        return false;
    if (candidate == m_workspaceState)
        return activateTerminalPane(QString::fromStdString(transfer.sourcePaneId));
    if (!saveWorkspaceStateCandidate(candidate))
        return false;

    m_workspaceState = std::move(candidate);
    publishTerminalOwnership();
    return true;
}

bool AppController::terminalWorkspaceCanTransfer(const QString &workspaceId) const
{
    if (m_shutdownStarted || !m_hostKeyTabId.isEmpty())
        return false;
    return std::ranges::none_of(m_tabs, [&](const auto &session) {
        const auto view = m_terminalViewports.value(session->paneId);
        return session->workspaceId == workspaceId && view && view->hasPreeditText();
    });
}

QString AppController::detachTerminalPane(const QString &paneId)
{
    const auto *session = findTabForPane(paneId);
    if (!session || !terminalWorkspaceCanTransfer(session->workspaceId))
        return {};
    const auto *source = findTerminalWorkspace(session->workspaceId);
    if (!source)
        return {};
    if (source->restoreIntents.size() == 1)
        return detachTerminalWorkspace(session->workspaceId) ? session->workspaceId : QString{};
    auto candidate = m_workspaceState;
    const auto newId = newLayoutId();
    const auto original = source->id;
    if (!workbench::transferTerminalWorkspace(candidate, {.kind = workbench::TerminalTransferKind::ExtractPane,
                                                          .sourceWorkspaceId = original,
                                                          .sourcePaneId = paneId.toStdString(),
                                                          .targetWorkspaceId = newId}))
        return {};
    auto &created = candidate.terminalWorkspaces.back();
    created.windowId = newLayoutId();
    created.returnWorkspaceId = original;
    if (!saveWorkspaceStateCandidate(candidate))
        return {};
    m_workspaceState = std::move(candidate);
    publishTerminalOwnership();
    return QString::fromStdString(newId);
}

bool AppController::detachTerminalWorkspace(const QString &workspaceId)
{
    if (!terminalWorkspaceCanTransfer(workspaceId))
        return false;
    auto candidate = m_workspaceState;
    const auto found = std::ranges::find(candidate.terminalWorkspaces, workspaceId.toStdString(),
                                         &workbench::TerminalWorkspaceLayout::id);
    if (found == candidate.terminalWorkspaces.end())
        return false;
    if (found->windowId != "main")
        return true;
    // Detached windows currently expose one pane, not another tab workspace.
    if (found->restoreIntents.size() != 1)
        return false;
    found->windowId = newLayoutId();
    found->returnWorkspaceId.clear();
    if (!saveWorkspaceStateCandidate(candidate))
        return false;
    m_workspaceState = std::move(candidate);
    publishTerminalOwnership();
    return true;
}

bool AppController::reattachTerminalWorkspace(const QString &workspaceId)
{
    const auto *source = findTerminalWorkspace(workspaceId);
    if (!source || source->windowId == "main" || !terminalWorkspaceCanTransfer(workspaceId))
        return false;
    auto candidate = m_workspaceState;
    const auto *destination = findTerminalWorkspace(QString::fromStdString(source->returnWorkspaceId));
    if (source->restoreIntents.size() == 1 && destination && destination->id != source->id
        && destination->restoreIntents.size() < workbench::maximumTerminalPanesPerWorkspace)
    {
        if (!terminalWorkspaceCanTransfer(QString::fromStdString(destination->id))
            || !workbench::transferTerminalWorkspace(candidate,
                                                     {.kind = workbench::TerminalTransferKind::MergeWorkspace,
                                                      .sourceWorkspaceId = source->id,
                                                      .targetWorkspaceId = destination->id,
                                                      .targetPaneId = destination->activePaneId,
                                                      .splitNodeId = newLayoutId()}))
            return false;
    }
    else
    {
        auto &layout =
            *std::ranges::find(candidate.terminalWorkspaces, source->id, &workbench::TerminalWorkspaceLayout::id);
        layout.windowId = "main";
        layout.returnWorkspaceId.clear();
        candidate.activeTerminalWorkspaceId = layout.id;
    }
    if (!saveWorkspaceStateCandidate(candidate))
        return false;
    m_workspaceState = std::move(candidate);
    publishTerminalOwnership();
    return true;
}

bool AppController::insertTerminalWorkspace(const QString &workspaceId, const int targetIndex)
{
    if (!terminalWorkspaceCanTransfer(workspaceId) || targetIndex < 0)
        return false;
    auto candidate = m_workspaceState;
    const auto found = std::ranges::find(candidate.terminalWorkspaces, workspaceId.toStdString(),
                                         &workbench::TerminalWorkspaceLayout::id);
    if (found == candidate.terminalWorkspaces.end())
        return false;
    auto moved = std::move(*found);
    candidate.terminalWorkspaces.erase(found);
    moved.windowId = "main";
    moved.returnWorkspaceId.clear();
    auto destination = candidate.terminalWorkspaces.end();
    int index = 0;
    for (auto it = candidate.terminalWorkspaces.begin(); it != candidate.terminalWorkspaces.end(); ++it)
        if (it->windowId == "main" && index++ == targetIndex)
        {
            destination = it;
            break;
        }
    candidate.terminalWorkspaces.insert(destination, std::move(moved));
    candidate.activeTerminalWorkspaceId = workspaceId.toStdString();
    if (!saveWorkspaceStateCandidate(candidate))
        return false;
    m_workspaceState = std::move(candidate);
    publishTerminalOwnership();
    return true;
}

bool AppController::terminalWorkspaceHasActiveSessions(const QString &workspaceId) const
{
    return std::ranges::any_of(m_tabs, [&](const auto &session) {
        return session->workspaceId == workspaceId
               && (session->running || session->reconnectPending
                   || terminalTabValue(*session, workspaceId).value(QStringLiteral("connecting")).toBool());
    });
}

void AppController::publishTerminalOwnership()
{
    m_pinnedTerminalWorkspaceIds.removeIf([this](const QString &id) {
        return findTerminalWorkspace(id) == nullptr;
    });
    // m_tabs is the existing owning session registry. Its stable objects, workers,
    // snapshots and sidecars survive; only their layout membership changes.
    for (const auto &layout : m_workspaceState.terminalWorkspaces)
    {
        for (const auto &node : layout.nodes)
        {
            if (node.kind != workbench::TerminalLayoutNodeKind::Leaf)
                continue;
            if (auto *session = findTabForPane(QString::fromStdString(node.id)))
                session->workspaceId = QString::fromStdString(layout.id);
        }
    }
    m_activeTabId = QString::fromStdString(m_workspaceState.activeTerminalWorkspaceId);
    const auto *layout = findTerminalWorkspace(m_activeTabId);
    const auto *session = layout ? findTabForPane(QString::fromStdString(layout->activePaneId)) : nullptr;
    m_focusedTabId = session ? session->id : QString{};
    m_terminal = session ? m_terminalViewports.value(session->paneId) : nullptr;
    emit terminalTabsChanged();
    emitActiveTerminalContextChanged();
}

bool AppController::moveTerminalPane(const QString &paneId, const QString &targetPaneId, const QString &orientation,
                                     const bool placeAfter)
{
    const auto *source = findTabForPane(paneId);
    const auto *target = findTabForPane(targetPaneId);
    if (!source || !target || source == target
        || (orientation != QStringLiteral("swap") && orientation != QStringLiteral("horizontal")
            && orientation != QStringLiteral("vertical")))
        return false;
    const auto *targetLayout = findTerminalWorkspace(target->workspaceId);
    if (!targetLayout || targetLayout->windowId != "main")
        return false;
    return applyTerminalTransfer(
        {.kind = orientation == QStringLiteral("swap") ? workbench::TerminalTransferKind::SwapPanes
                                                       : workbench::TerminalTransferKind::MovePane,
         .sourceWorkspaceId = source->workspaceId.toStdString(),
         .sourcePaneId = paneId.toStdString(),
         .targetWorkspaceId = target->workspaceId.toStdString(),
         .targetPaneId = targetPaneId.toStdString(),
         .splitNodeId = newLayoutId(),
         .orientation = orientation == QStringLiteral("vertical") ? workbench::TerminalSplitOrientation::Vertical
                                                                  : workbench::TerminalSplitOrientation::Horizontal,
         .placeAfter = placeAfter});
}

QString AppController::extractTerminalPaneToTab(const QString &paneId)
{
    const auto *source = findTabForPane(paneId);
    if (!source)
        return {};
    const auto *layout = findTerminalWorkspace(source->workspaceId);
    if (!layout)
        return {};
    if (layout->restoreIntents.size() == 1)
    {
        static_cast<void>(activateTerminalPane(paneId));
        return source->workspaceId;
    }
    const auto newId = newLayoutId();
    return applyTerminalTransfer({.kind = workbench::TerminalTransferKind::ExtractPane,
                                  .sourceWorkspaceId = source->workspaceId.toStdString(),
                                  .sourcePaneId = paneId.toStdString(),
                                  .targetWorkspaceId = newId})
               ? QString::fromStdString(newId)
               : QString{};
}

bool AppController::mergeTerminalWorkspace(const QString &workspaceId, const QString &targetPaneId,
                                           const QString &orientation, const bool placeAfter)
{
    const auto *target = findTabForPane(targetPaneId);
    if (!target || (orientation != QStringLiteral("horizontal") && orientation != QStringLiteral("vertical")))
        return false;
    const auto *targetLayout = findTerminalWorkspace(target->workspaceId);
    if (!targetLayout || targetLayout->windowId != "main")
        return false;
    return applyTerminalTransfer({.kind = workbench::TerminalTransferKind::MergeWorkspace,
                                  .sourceWorkspaceId = workspaceId.toStdString(),
                                  .targetWorkspaceId = target->workspaceId.toStdString(),
                                  .targetPaneId = targetPaneId.toStdString(),
                                  .splitNodeId = newLayoutId(),
                                  .orientation = orientation == QStringLiteral("vertical")
                                                     ? workbench::TerminalSplitOrientation::Vertical
                                                     : workbench::TerminalSplitOrientation::Horizontal,
                                  .placeAfter = placeAfter});
}
} // namespace ztermy
