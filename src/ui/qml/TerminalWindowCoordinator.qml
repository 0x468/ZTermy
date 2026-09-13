pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: coordinator
    required property var hostRoot
    required property var titleTabs
    required property var newTabButton
    required property var terminalArea
    property var windows: ({})
    property var dropTarget: ({})
    property var movingWindow: null
    property string draggedPaneId: ""
    property var preparedWindow: null
    property Component windowComponent: Component {
        DetachedTerminalWindow {
            hostRoot: coordinator.hostRoot
        }
    }
    property Connections controllerSignals: Connections {
        target: coordinator.hostRoot.controller
        function onTerminalTabsChanged() {
            Qt.callLater(coordinator.syncWindows);
        }
        function onTerminalWorkspaceChanged() {
            Qt.callLater(coordinator.syncWindows);
        }
    }

    Component.onCompleted: Qt.callLater(syncWindows)
    Component.onDestruction: {
        for (const id in windows)
            windows[id].destroy();
    }

    function prepareWindow() {
        preparedWindow = windowComponent.createObject(hostRoot);
        if (!preparedWindow)
            return false;
        hostRoot.windowChrome.configureDetachedWindow(preparedWindow);
        return true;
    }

    function detachPane(paneId) {
        if (!prepareWindow())
            return "";
        const id = hostRoot.controller.detachTerminalPane(paneId);
        finishPreparation(id);
        return id;
    }

    function finishPreparation(id) {
        if (!id || windows[id]) {
            preparedWindow.destroy();
            preparedWindow = null;
        } else {
            windows[id] = preparedWindow;
            preparedWindow.workspaceId = id;
            preparedWindow = null;
        }
        syncWindows();
        if (id && windows[id]) {
            windows[id].raise();
            windows[id].requestActivate();
        }
    }

    function syncWindows() {
        const live = ({});
        for (const tab of hostRoot.controller.terminalTabs) {
            if (!tab.windowId || tab.windowId === "main")
                continue;
            live[tab.id] = true;
            if (!windows[tab.id]) {
                const created = windowComponent.createObject(hostRoot, {
                    workspaceId: tab.id
                });
                if (!created) {
                    hostRoot.controller.reattachTerminalWorkspace(tab.id);
                    continue;
                }
                windows[tab.id] = created;
                hostRoot.windowChrome.configureDetachedWindow(created);
            }
            windows[tab.id].workspace = hostRoot.controller.terminalWorkspace(tab.id);
            if (!windows[tab.id].visible)
                windows[tab.id].show();
        }
        for (const id in windows) {
            if (!live[id]) {
                windows[id].hide();
                windows[id].destroy();
                delete windows[id];
            }
        }
    }

    function applyAppearance() {
        for (const id in windows)
            hostRoot.windowChrome.configureDetachedWindow(windows[id]);
    }

    function activateWorkspace(workspaceId) {
        syncWindows();
        if (windows[workspaceId]) {
            windows[workspaceId].raise();
            windows[workspaceId].requestActivate();
        }
    }

    function viewportAt(item, globalPosition, prefix = "terminalViewport-") {
        if (!item.visible)
            return null;
        if (String(item.objectName).startsWith(prefix)) {
            const p = item.mapFromGlobal(globalPosition.x, globalPosition.y);
            if (p.x >= 0 && p.y >= 0 && p.x < item.width && p.y < item.height)
                return item;
        }
        for (const child of item.children) {
            const found = viewportAt(child, globalPosition, prefix);
            if (found)
                return found;
        }
        return null;
    }

    function updateWindowDrop(window, globalPosition) {
        movingWindow = window;
        window.opacity = 0.72;
        updateDropTarget(globalPosition);
    }

    function updateDropTarget(globalPosition) {
        const point = hostRoot.mapFromGlobal(globalPosition.x, globalPosition.y);
        dropTarget = ({});
        if (!hostRoot.visible || point.x < 0 || point.y < 0 || point.x >= hostRoot.width || point.y >= hostRoot.height)
            return;
        const tabs = hostRoot.mainTerminalTabs;
        if (point.y < hostRoot.titleBarHeight) {
            for (let index = 0; index < tabs.length; ++index) {
                const item = titleTabs.itemAtIndex(index);
                if (!item)
                    continue;
                const origin = item.mapToItem(hostRoot, 0, 0);
                const strip = titleTabs.mapToItem(hostRoot, 0, 0);
                if (point.x < strip.x || point.x > strip.x + titleTabs.width)
                    continue;
                if (point.x >= origin.x - 4 && point.x <= origin.x + item.width + 4) {
                    const before = point.x < origin.x + 8;
                    const after = point.x > origin.x + item.width - 8;
                    if (before || after) {
                        dropTarget = {
                            mode: "insert",
                            index: index + (after ? 1 : 0),
                            x: after ? origin.x + item.width : origin.x,
                            y: 4,
                            width: 3,
                            height: hostRoot.titleBarHeight - 8
                        };
                    } else {
                        const workspace = hostRoot.controller.terminalWorkspace(tabs[index].id);
                        dropTarget = {
                            mode: "merge",
                            paneId: workspace.activePaneId,
                            orientation: "horizontal",
                            after: true,
                            x: origin.x,
                            y: 2,
                            width: item.width,
                            height: hostRoot.titleBarHeight - 4
                        };
                        // Preview the target without activating the main native window
                        // or stealing the Windows move loop's pointer capture.
                        if (hostRoot.requestedMainWorkspaceId !== tabs[index].id || hostRoot.currentPage !== "terminal") {
                            hostRoot.controller.activateTerminalTab(tabs[index].id);
                            hostRoot.requestedMainWorkspaceId = tabs[index].id;
                            hostRoot.refreshMainWorkspace();
                            hostRoot.currentPage = "terminal";
                        }
                    }
                    return;
                }
            }
            const plus = newTabButton.mapToItem(hostRoot, 0, 0);
            if (point.x >= plus.x && point.x <= plus.x + newTabButton.width)
                dropTarget = {
                    mode: "insert",
                    index: tabs.length,
                    x: plus.x,
                    y: 4,
                    width: 3,
                    height: hostRoot.titleBarHeight - 8
                };
            return;
        }
        if (hostRoot.currentPage !== "terminal")
            return;
        const view = viewportAt(terminalArea, globalPosition);
        if (!view)
            return;
        const p = view.mapFromGlobal(globalPosition.x, globalPosition.y);
        const paneId = String(view.objectName).substring("terminalViewport-".length);
        if (paneId === draggedPaneId)
            return;
        const horizontal = p.x < view.width * 0.25 || p.x > view.width * 0.75;
        const swap = draggedPaneId.length > 0 && !horizontal && p.y >= view.height * 0.25 && p.y <= view.height * 0.75;
        const after = horizontal ? p.x > view.width / 2 : p.y > view.height / 2;
        const origin = view.mapToItem(hostRoot, 0, 0);
        const width = horizontal ? view.width / 2 : view.width;
        const height = horizontal || swap ? view.height : view.height / 2;
        dropTarget = {
            mode: swap ? "swap" : "merge",
            paneId: paneId,
            orientation: horizontal ? "horizontal" : "vertical",
            after: after,
            x: origin.x + (horizontal && after ? width : 0),
            y: origin.y + (!horizontal && !swap && after ? height : 0),
            width: width,
            height: height
        };
    }

    function finishPaneDrop(paneId, outside) {
        const target = dropTarget;
        dropTarget = ({});
        draggedPaneId = "";
        Qt.callLater(() => {
            let ok = false;
            if (target.mode === "insert") {
                const id = hostRoot.controller.extractTerminalPaneToTab(paneId);
                if (id.length > 0) {
                    const oldIndex = hostRoot.mainTerminalTabs.findIndex(tab => tab.id === id);
                    const index = target.index - (oldIndex >= 0 && oldIndex < target.index ? 1 : 0);
                    ok = hostRoot.controller.insertTerminalWorkspace(id, index);
                }
            } else if (target.mode && target.paneId !== paneId) {
                ok = hostRoot.controller.moveTerminalPane(paneId, target.paneId, target.mode === "swap" ? "swap" : target.orientation, target.after);
            } else if (!target.mode && outside) {
                hostRoot.detachTerminalPane(paneId);
                return;
            }
            if (ok)
                hostRoot.activateMainTerminal(hostRoot.controller.activeTerminalTabId);
        });
    }

    function finishWindowDrop(window, cancelled) {
        if (movingWindow !== window)
            return;
        const target = dropTarget;
        const id = window.workspaceId;
        window.opacity = 1;
        movingWindow = null;
        dropTarget = ({});
        if (cancelled || !target.mode)
            return;
        Qt.callLater(() => {
            const ok = target.mode === "insert" ? hostRoot.controller.insertTerminalWorkspace(id, target.index) : hostRoot.controller.mergeTerminalWorkspace(id, target.paneId, target.orientation, target.after);
            if (ok) {
                hostRoot.activateMainTerminal(hostRoot.controller.activeTerminalTabId);
                hostRoot.windowChrome.show();
                hostRoot.windowChrome.raise();
                hostRoot.windowChrome.requestActivate();
            }
        });
    }
}
