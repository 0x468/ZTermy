pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: windowCoordinator
    required property var hostRoot
    required property var titleTabs
    required property var newTabButton
    required property var terminalArea
    property var windows: ({})
    property var dropTarget: ({})
    property string draggedPaneId: ""
    property var preparedWindow: null
    property var placementCache: ({})
    property bool trackingPlacement: false
    property bool applicationExiting: false
    property Timer placementTimer: Timer {
        interval: 200
        onTriggered: windowCoordinator.capturePlacements()
    }
    property Connections mainPlacementSignals: Connections {
        target: windowCoordinator.hostRoot.windowChrome
        function onXChanged() {
            windowCoordinator.schedulePlacement();
        }
        function onYChanged() {
            windowCoordinator.schedulePlacement();
        }
        function onWidthChanged() {
            windowCoordinator.schedulePlacement();
        }
        function onHeightChanged() {
            windowCoordinator.schedulePlacement();
        }
        function onWindowStateChanged() {
            windowCoordinator.schedulePlacement();
        }
    }
    property Component windowComponent: Component {
        DetachedTerminalWindow {
            hostRoot: windowCoordinator.hostRoot
            coordinator: windowCoordinator
        }
    }
    property Connections controllerSignals: Connections {
        target: windowCoordinator.hostRoot.controller
        function onTerminalTabsChanged() {
            Qt.callLater(windowCoordinator.syncWindows);
            windowCoordinator.schedulePlacement();
        }
        function onTerminalWorkspaceChanged() {
            Qt.callLater(windowCoordinator.syncWindows);
            windowCoordinator.schedulePlacement();
        }
        function onTerminalWindowStateRequested() {
            windowCoordinator.capturePlacements();
        }
    }

    Component.onCompleted: {
        const saved = restorePlacement(hostRoot.windowChrome, "main");
        if (hostRoot.mainTerminalTabs.some(tab => tab.id === saved.selectedWorkspaceId))
            hostRoot.requestedMainWorkspaceId = saved.selectedWorkspaceId;
        trackingPlacement = true;
        Qt.callLater(syncWindows);
        schedulePlacement();
    }
    Component.onDestruction: {
        for (const id in windows)
            windows[id].destroy();
    }

    function restorePlacement(window, id) {
        const saved = hostRoot.controller.terminalWindowState(id);
        placementCache[id] = WindowControl.restorePlacement(window, saved);
        return saved;
    }
    function schedulePlacement() {
        if (trackingPlacement)
            placementTimer.restart();
    }
    function capturePlacements() {
        if (!trackingPlacement)
            return;
        const capture = (window, id, selected) => {
            const value = WindowControl.placement(window, placementCache[id] || ({}));
            if (!value.width || !value.height)
                return;
            placementCache[id] = value;
            hostRoot.controller.rememberTerminalWindow(Object.assign({}, value, {
                id: id,
                selectedWorkspaceId: selected
            }));
        };
        capture(hostRoot.windowChrome, "main", hostRoot.mainWorkspaceId);
        for (const id in windows)
            capture(windows[id], id, windows[id].workspaceId);
    }

    function beginApplicationExit() {
        if (applicationExiting)
            return;
        capturePlacements();
        applicationExiting = true;
        trackingPlacement = false;
        placementTimer.stop();
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
        const owner = id ? hostRoot.controller.terminalWorkspace(id).windowId : "";
        if (!owner || windows[owner]) {
            preparedWindow.destroy();
            preparedWindow = null;
        } else {
            windows[owner] = preparedWindow;
            preparedWindow.ownerWindowId = owner;
            preparedWindow.workspaceId = id;
            preparedWindow = null;
        }
        syncWindows();
        if (owner && windows[owner])
            WindowControl.present(windows[owner]);
    }

    function syncWindows() {
        if (applicationExiting)
            return;
        const live = ({});
        for (const tab of hostRoot.controller.terminalTabs) {
            if (!tab.windowId || tab.windowId === "main")
                continue;
            if (!live[tab.windowId])
                live[tab.windowId] = [];
            live[tab.windowId].push(tab);
        }
        for (const id in live) {
            if (!windows[id]) {
                const created = windowComponent.createObject(hostRoot, {
                    ownerWindowId: id
                });
                if (!created) {
                    for (const tab of live[id])
                        hostRoot.controller.insertTerminalWorkspace(tab.id, hostRoot.mainTerminalTabs.length);
                    continue;
                }
                windows[id] = created;
                hostRoot.windowChrome.configureDetachedWindow(created);
            }
            const window = windows[id];
            window.tabs = live[id];
            if (!placementCache[id]) {
                const saved = restorePlacement(window, id);
                if (live[id].some(tab => tab.id === saved.selectedWorkspaceId))
                    window.workspaceId = saved.selectedWorkspaceId;
            }
            const activeId = hostRoot.controller.activeTerminalTabId;
            if (live[id].some(tab => tab.id === activeId))
                window.workspaceId = activeId;
            else if (!live[id].some(tab => tab.id === window.workspaceId))
                window.workspaceId = live[id][0].id;
            window.workspace = hostRoot.controller.terminalWorkspace(window.workspaceId);
            if (!window.visible)
                WindowControl.reveal(window);
        }
        for (const id in windows) {
            if (!live[id]) {
                windows[id].hide();
                windows[id].destroy();
                delete windows[id];
                delete placementCache[id];
            }
        }
    }

    function applyAppearance() {
        for (const id in windows)
            hostRoot.windowChrome.configureDetachedWindow(windows[id]);
    }

    function reattachAll(window) {
        const ids = window.tabs.map(tab => tab.id);
        const selected = window.workspaceId;
        for (const id of ids) {
            if (!hostRoot.controller.insertTerminalWorkspace(id, hostRoot.controller.terminalTabs.filter(tab => tab.windowId === "main").length, "main"))
                return;
        }
        hostRoot.activateMainTerminal(selected);
        WindowControl.present(hostRoot.windowChrome);
    }

    function clearTabPreviews(keepWindowId = "") {
        for (const id in windows) {
            if (id === keepWindowId)
                windows[id].tabBarVisible = true;
            windows[id].tabBarPreview = false;
        }
    }

    function activateWorkspace(workspaceId) {
        syncWindows();
        const owner = hostRoot.controller.terminalWorkspace(workspaceId).windowId;
        if (windows[owner]) {
            windows[owner].selectWorkspace(workspaceId);
            WindowControl.present(windows[owner]);
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

    function updateDropTarget(globalPosition) {
        dropTarget = ({});
        for (const id in windows) {
            const window = windows[id];
            const p = window.contentItem.mapFromGlobal(globalPosition.x, globalPosition.y);
            window.tabBarPreview = !window.tabBarVisible && p.x >= 0 && p.x < window.width && p.y >= 0 && p.y < (window.tabBarPreview ? 32 : 20) && WindowControl.acceptsDropAt(window, p, null);
        }
        for (const id in windows) {
            const window = windows[id];
            if (!window.visible)
                continue;
            const p = window.contentItem.mapFromGlobal(globalPosition.x, globalPosition.y);
            if (p.x < 0 || p.y < 0 || p.x >= window.width || p.y >= window.height)
                continue;
            if (!WindowControl.acceptsDropAt(window, p, null))
                continue;
            if ((window.tabBarVisible || window.tabBarPreview) && p.y < 32) {
                dropTarget = window.tabDropTarget(globalPosition);
            } else {
                const view = viewportAt(window.contentItem, globalPosition);
                if (view)
                    dropTarget = paneDropTarget(view, globalPosition, window.contentItem, id);
            }
            return;
        }
        updateMainDropTarget(globalPosition);
        if (dropTarget.mode)
            dropTarget = Object.assign({}, dropTarget, {
                windowId: "main"
            });
    }

    function paneDropTarget(view, globalPosition, surface, windowId) {
        const p = view.mapFromGlobal(globalPosition.x, globalPosition.y);
        const paneId = String(view.objectName).substring("terminalViewport-".length);
        if (paneId === draggedPaneId)
            return ({});
        const horizontal = p.x < view.width * 0.25 || p.x > view.width * 0.75;
        const swap = draggedPaneId.length > 0 && !horizontal && p.y >= view.height * 0.25 && p.y <= view.height * 0.75;
        const after = horizontal ? p.x > view.width / 2 : p.y > view.height / 2;
        const origin = view.mapToItem(surface, 0, 0);
        const width = horizontal ? view.width / 2 : view.width;
        const height = horizontal || swap ? view.height : view.height / 2;
        return {
            mode: swap ? "swap" : "merge",
            windowId: windowId,
            paneId: paneId,
            orientation: horizontal ? "horizontal" : "vertical",
            after: after,
            x: origin.x + (horizontal && after ? width : 0),
            y: origin.y + (!horizontal && !swap && after ? height : 0),
            width: width,
            height: height
        };
    }

    function updateMainDropTarget(globalPosition) {
        const point = hostRoot.mapFromGlobal(globalPosition.x, globalPosition.y);
        dropTarget = ({});
        if (!hostRoot.visible || point.x < 0 || point.y < 0 || point.x >= hostRoot.width || point.y >= hostRoot.height)
            return;
        if (!WindowControl.acceptsDropAt(hostRoot.windowChrome, point, null))
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
        dropTarget = paneDropTarget(view, globalPosition, hostRoot, "main");
    }

    function presentTarget(target) {
        if (target.windowId && target.windowId !== "main")
            activateWorkspace(hostRoot.controller.activeTerminalTabId);
        else {
            hostRoot.activateMainTerminal(hostRoot.controller.activeTerminalTabId);
            WindowControl.present(hostRoot.windowChrome);
        }
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
                    const targetTabs = hostRoot.controller.terminalTabs.filter(tab => tab.windowId === target.windowId);
                    const oldIndex = targetTabs.findIndex(tab => tab.id === id);
                    const index = target.index - (oldIndex >= 0 && oldIndex < target.index ? 1 : 0);
                    ok = hostRoot.controller.insertTerminalWorkspace(id, index, target.windowId || "main");
                }
            } else if (target.mode && target.paneId !== paneId) {
                ok = hostRoot.controller.moveTerminalPane(paneId, target.paneId, target.mode === "swap" ? "swap" : target.orientation, target.after);
            } else if (!target.mode && outside) {
                hostRoot.detachTerminalPane(paneId);
                return;
            }
            clearTabPreviews(ok && target.mode === "insert" ? target.windowId : "");
            if (ok)
                presentTarget(target);
        });
    }

    function finishTabDrop(id, outside) {
        const target = dropTarget;
        dropTarget = ({});
        Qt.callLater(() => {
            let ok = false;
            if (target.mode === "insert") {
                const owner = target.windowId || "main";
                const tabs = hostRoot.controller.terminalTabs.filter(tab => tab.windowId === owner);
                const oldIndex = tabs.findIndex(tab => tab.id === id);
                const index = target.index - (oldIndex >= 0 && oldIndex < target.index ? 1 : 0);
                ok = hostRoot.controller.insertTerminalWorkspace(id, index, owner);
            } else if (target.mode === "merge") {
                ok = hostRoot.controller.mergeTerminalWorkspace(id, target.paneId, target.orientation, target.after);
            } else if (outside && prepareWindow()) {
                finishPreparation(hostRoot.controller.detachTerminalWorkspace(id) ? id : "");
                return;
            }
            clearTabPreviews(ok && target.mode === "insert" ? target.windowId : "");
            if (ok)
                presentTarget(target);
        });
    }
}
