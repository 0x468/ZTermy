pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: detachedTerminalWindow
    objectName: "detachedTerminalWindow"
    required property var hostRoot
    required property var coordinator
    property string ownerWindowId: ""
    property var tabs: []
    property string workspaceId: ""
    property var workspace: ({})
    readonly property string zoomedPaneId: hostRoot.paneZoomByWorkspace[workspaceId] || ""
    readonly property var visibleLayoutRoot: zoomedPaneId.length > 0 ? hostRoot.findTerminalPane(workspace.root, zoomedPaneId) || workspace.root : workspace.root
    property var pendingPasteViewport: null
    property int pendingPasteLineCount: 0
    property bool tabBarVisible: false
    property bool tabBarPreview: false
    property bool nativeMaximizeButtonHovered: false
    property bool nativeMaximizeButtonPressed: false
    property string hostMainSelection: ""
    property string hostConnectionError: ""
    readonly property var controller: hostRoot.controller
    readonly property string currentPage: "terminal"
    readonly property bool maximized: visibility === Window.Maximized
    readonly property var localActionIds: ["terminal.newLocal", "tabs.duplicate", "terminal.find", "tabs.next", "tabs.previous", "tabs.close", "terminal.splitHorizontal", "terminal.splitVertical", "terminal.duplicatePane", "terminal.focusNextPane", "terminal.focusPreviousPane", "terminal.growPane", "terminal.shrinkPane", "terminal.swapNextPane", "terminal.swapPreviousPane"]

    Repeater {
        model: detachedTerminalWindow.controller.actions.filter(action => detachedTerminalWindow.localActionIds.includes(action.id))
        Item {
            id: localShortcut
            required property var modelData
            width: 0
            height: 0
            Shortcut {
                sequence: localShortcut.modelData.shortcut
                enabled: detachedTerminalWindow.active && localShortcut.modelData.enabled && sequence.length > 0 && !renameDialog.visible
                autoRepeat: localShortcut.modelData.autoRepeat
                context: Qt.WindowShortcut
                onActivated: detachedTerminalWindow.executeLocalAction(localShortcut.modelData.id)
            }
        }
    }

    function executeLocalAction(id) {
        if (!active || !controller.activateTerminalTab(workspaceId))
            return;
        switch (id) {
        case "terminal.newLocal":
            openLocalTab();
            break;
        case "tabs.duplicate":
            openLocalTab(workspaceId);
            break;
        case "terminal.find":
            if (searchPanel.visible)
                searchPanel.closeSearch();
            else
                openTerminalSearch();
            break;
        case "tabs.next":
        case "tabs.previous":
            const index = tabs.findIndex(tab => tab.id === workspaceId);
            if (index >= 0 && tabs.length > 0)
                selectWorkspace(tabs[(index + (id === "tabs.next" ? 1 : -1) + tabs.length) % tabs.length].id);
            break;
        case "tabs.close":
            if ((workspace.paneCount || 1) > 1)
                controller.closeActiveTerminalPane();
            else
                closeTab(workspaceId);
            break;
        case "terminal.splitHorizontal":
        case "terminal.splitVertical":
        case "terminal.duplicatePane":
            controller.splitActiveTerminal(id === "terminal.duplicatePane" ? "auto" : id === "terminal.splitVertical" ? "vertical" : "horizontal", id === "terminal.duplicatePane");
            break;
        case "terminal.focusNextPane":
        case "terminal.focusPreviousPane":
            controller.focusRelativeTerminalPane(id === "terminal.focusNextPane" ? 1 : -1);
            Qt.callLater(() => {
                if (detachedTerminalWindow.active)
                    detachedViewport.forceActiveFocus();
            });
            break;
        case "terminal.growPane":
        case "terminal.shrinkPane":
            controller.resizeActiveTerminalPane(id === "terminal.growPane" ? 0.05 : -0.05);
            break;
        case "terminal.swapNextPane":
        case "terminal.swapPreviousPane":
            controller.swapActiveTerminalPane(id === "terminal.swapNextPane" ? 1 : -1);
            break;
        }
    }

    function openLocalTab(sourceId = "", shellId = "") {
        const mainSelection = hostRoot.requestedMainWorkspaceId;
        const insertionIndex = tabs.findIndex(tab => tab.id === (sourceId || workspaceId)) + 1;
        const created = sourceId.length > 0 ? controller.duplicateTerminalTab(sourceId) : controller.startLocalTerminalWithShell(shellId).length > 0;
        if (!created)
            return false;
        const placed = controller.insertTerminalWorkspace(controller.activeTerminalTabId, insertionIndex, ownerWindowId);
        if (placed) {
            hostRoot.requestedMainWorkspaceId = mainSelection;
            Qt.callLater(hostRoot.refreshMainWorkspace);
        }
        return placed;
    }
    function selectWorkspace(id) {
        if (!tabs.some(tab => tab.id === id))
            return;
        workspaceId = id;
        controller.activateTerminalTab(id);
        workspace = controller.terminalWorkspace(id);
    }
    function openTerminalSearch() {
        searchPanel.openSearch();
    }
    function toggleTabBar() {
        tabBarVisible = !tabBarVisible;
    }
    function focusTerminalAfterLayout() {
        controller.activateTerminalTab(workspaceId);
    }
    function closeTab(id) {
        const index = tabs.findIndex(tab => tab.id === id);
        if (index < 0)
            return;
        const other = tabs[index + 1] || tabs[index - 1];
        controller.closeTerminalTab(id, other ? other.id : hostRoot.requestedMainWorkspaceId);
    }
    function tabInsertionIndex(globalPosition) {
        const p = tabRow.mapFromGlobal(globalPosition.x, globalPosition.y);
        return Math.max(0, Math.min(tabs.length, Math.floor((p.x + 80) / 160)));
    }

    function tabDropTarget(globalPosition) {
        const p = tabScroll.mapFromGlobal(globalPosition.x, globalPosition.y);
        if (p.x < 0 || p.x > tabScroll.width + 32)
            return ({});
        const rowPoint = tabRow.mapFromGlobal(globalPosition.x, globalPosition.y);
        const index = Math.floor(rowPoint.x / 160);
        const offset = rowPoint.x - index * 160;
        const insertion = p.x >= tabScroll.width || index >= tabs.length || offset < 8 || offset > 152;
        if (insertion)
            return {
                mode: "insert",
                windowId: ownerWindowId,
                index: Math.min(tabs.length, index + (offset > 152 ? 1 : 0)),
                x: p.x,
                y: 4,
                width: 3,
                height: 24
            };
        const tab = tabs[index];
        if (!tab)
            return ({});
        // Preview a tab without activating this native window or taking the
        // source drag's pointer capture.
        if (workspaceId !== tab.id)
            selectWorkspace(tab.id);
        return {
            mode: "merge",
            windowId: ownerWindowId,
            paneId: workspace.activePaneId,
            orientation: "horizontal",
            after: true,
            x: index * 160 - tabScroll.contentX,
            y: 2,
            width: 160,
            height: 28
        };
    }

    transientParent: null
    // Same native style as the main window: Windows owns maximize, minimize
    // and the restore-to-maximized placement, while the chrome's native event
    // filter removes the frame. A frameless window only gets moved to the
    // work area on maximize, so the native state never matches the Qt state.
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    width: 920
    height: 620
    minimumWidth: 480
    minimumHeight: 320
    visible: false
    onXChanged: coordinator.schedulePlacement()
    onYChanged: coordinator.schedulePlacement()
    onWidthChanged: coordinator.schedulePlacement()
    onHeightChanged: coordinator.schedulePlacement()
    onWindowStateChanged: coordinator.schedulePlacement()
    onActiveChanged: {
        if (active && workspaceId.length > 0)
            hostRoot.controller.activateTerminalTab(workspaceId);
    }
    title: qsTr("%1 — Detached pane").arg(workspace.title || qsTr("Terminal"))
    color: Theme.workspaceBackground
    onClosing: close => {
        if (coordinator.applicationExiting) {
            close.accepted = true;
            return;
        }
        close.accepted = false;
        const ids = tabs.map(tab => tab.id);
        Qt.callLater(() => {
            if (coordinator.applicationExiting)
                return;
            for (const id of ids)
                controller.closeTerminalTab(id, hostRoot.requestedMainWorkspaceId);
        });
    }

    Item {
        id: tabBar
        objectName: "detachedTabBar"
        width: parent.width
        visible: detachedTerminalWindow.tabBarVisible || detachedTerminalWindow.tabBarPreview
        enabled: visible
        onVisibleChanged: {
            if (!visible) {
                detachedTerminalWindow.nativeMaximizeButtonHovered = false;
                detachedTerminalWindow.nativeMaximizeButtonPressed = false;
            }
        }
        height: visible ? 32 : 0
        MouseArea {
            anchors.fill: parent
            onPressed: {
                detachedTerminalWindow.startSystemMove();
            }
            onDoubleClicked: WindowControl.toggleMaximize(detachedTerminalWindow)
        }
        Flickable {
            id: tabScroll
            objectName: "detachedTabScroll"
            width: Math.min(tabRow.width, Math.max(0, parent.width - 160))
            height: parent.height
            contentWidth: tabRow.width
            interactive: false
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            WheelHandler {
                target: null
                blocking: true
                onWheel: event => {
                    const delta = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.x !== 0 ? event.angleDelta.x / 2 : event.angleDelta.y / 2;
                    tabScroll.contentX = Math.max(0, Math.min(tabScroll.contentWidth - tabScroll.width, tabScroll.contentX - delta));
                    event.accepted = true;
                }
            }
            Row {
                id: tabRow
                Repeater {
                    model: detachedTerminalWindow.tabs
                    delegate: TerminalTabAction {
                        required property var modelData
                        title: modelData.title
                        workspaceId: modelData.id
                        objectName: "workspaceTitle-" + modelData.id
                        selected: detachedTerminalWindow.workspaceId === modelData.id
                        width: 160
                        height: 32
                        iconName: modelData.iconName || "terminal"
                        running: modelData.running
                        connecting: modelData.connecting === true
                        progressState: modelData.progressState || 0
                        progressPercentage: modelData.progressPercentage ?? -1
                        doubleClickAction: detachedTerminalWindow.controller.windowInteractionSettings.tabDoubleClick
                        closeButtonMode: detachedTerminalWindow.controller.windowInteractionSettings.tabCloseButton
                        canMoveLeft: modelData.canMoveLeft
                        canReconnect: modelData.canReconnect
                        canDuplicate: modelData.canDuplicate
                        canMoveRight: modelData.canMoveRight
                        canCloseOthers: modelData.canCloseOthers
                        canCloseToRight: modelData.canCloseToRight
                        onActivated: detachedTerminalWindow.selectWorkspace(modelData.id)
                        onCloseRequested: detachedTerminalWindow.closeTab(modelData.id)
                        onRenameRequested: renameDialog.openFor(modelData.id, modelData.title)
                        onReconnectRequested: {
                            detachedTerminalWindow.selectWorkspace(modelData.id);
                            detachedTerminalWindow.controller.reconnectTerminalTab(modelData.sessionId);
                        }
                        onDuplicateRequested: detachedTerminalWindow.openLocalTab(modelData.id)
                        onMoveLeftRequested: detachedTerminalWindow.controller.moveTerminalTab(modelData.id, modelData.tabIndex - 1)
                        onDragPositionChanged: globalPosition => detachedTerminalWindow.coordinator.updateDropTarget(globalPosition)
                        onDragCanceled: {
                            detachedTerminalWindow.coordinator.dropTarget = ({});
                            detachedTerminalWindow.coordinator.clearTabPreviews();
                        }
                        onDragFinished: (sceneX, sceneY) => {
                            if (!dropCompleted) {
                                const outside = sceneX < 0 || sceneY < 0 || sceneX >= detachedTerminalWindow.width || sceneY >= detachedTerminalWindow.height;
                                detachedTerminalWindow.coordinator.finishTabDrop(modelData.id, outside);
                            }
                        }
                        onMoveRightRequested: detachedTerminalWindow.controller.moveTerminalTab(modelData.id, modelData.tabIndex + 1)
                        onCloseOthersRequested: detachedTerminalWindow.controller.closeOtherTerminalTabs(modelData.id)
                        onCloseToRightRequested: detachedTerminalWindow.controller.closeTerminalTabsToRight(modelData.id)
                    }
                }
            }
        }
        TitleChromeAction {
            id: newTerminalButton
            objectName: "detachedNewTerminalButton"
            x: tabScroll.width
            width: 32
            height: 32
            iconName: "plus"
            accessibleName: qsTranslate("Main", "New terminal")
            toolTip: accessibleName
            menuOpen: newTerminalMenu.visible
            onActivated: newTerminalMenu.open()
            TerminalNewMenu {
                id: newTerminalMenu
                objectName: "detachedNewTerminalMenu"
                y: parent.height
                controller: detachedTerminalWindow.controller
                onLocalRequested: shellId => detachedTerminalWindow.openLocalTab("", shellId)
                onHostRequested: profile => {
                    detachedTerminalWindow.hostConnectionError = "";
                    savedHostConnection.connectSaved(profile, newTerminalButton);
                }
            }
        }
        TitleChromeAction {
            objectName: "detachedReattachAllButton"
            x: newTerminalButton.x + newTerminalButton.width
            width: 32
            height: 32
            iconName: "external-link"
            accessibleName: qsTr("Reattach all tabs to main window")
            toolTip: accessibleName
            onActivated: detachedTerminalWindow.coordinator.reattachAll(detachedTerminalWindow)
        }
        Row {
            anchors.right: parent.right
            Repeater {
                model: ["minimize", "maximize", "close"]
                delegate: CaptionButton {
                    required property string modelData
                    objectName: "detachedWindowAction-" + modelData
                    width: 32
                    height: 32
                    kind: modelData
                    chrome: detachedTerminalWindow
                    externallyHovered: modelData === "maximize" && detachedTerminalWindow.nativeMaximizeButtonHovered
                    externallyPressed: modelData === "maximize" && detachedTerminalWindow.nativeMaximizeButtonPressed
                    accessibleName: modelData === "minimize" ? qsTranslate("TitleWindowActions", "Minimize") : modelData === "close" ? qsTranslate("TitleWindowActions", "Close") : detachedTerminalWindow.maximized ? qsTranslate("TitleWindowActions", "Restore") : qsTranslate("TitleWindowActions", "Maximize")
                    onActivated: {
                        if (modelData === "minimize")
                            WindowControl.minimize(detachedTerminalWindow);
                        else if (modelData === "maximize")
                            WindowControl.toggleMaximize(detachedTerminalWindow);
                        else
                            detachedTerminalWindow.close();
                    }
                }
            }
        }
    }

    TerminalSplitNode {
        id: detachedViewport
        objectName: "detachedWorkspaceViewport"
        anchors.fill: parent
        anchors.topMargin: tabBar.height
        controller: detachedTerminalWindow.hostRoot.controller
        node: detachedTerminalWindow.visibleLayoutRoot || ({})
        zoomedPaneId: detachedTerminalWindow.zoomedPaneId
        paneCount: detachedTerminalWindow.workspace.paneCount || 1
        defaultFontFamily: detachedTerminalWindow.hostRoot.controller.terminalFontFamily
        defaultFontSize: detachedTerminalWindow.hostRoot.controller.terminalFontSize
        defaultLigatures: detachedTerminalWindow.hostRoot.controller.terminalLigatures
        defaultBackgroundOpacity: 0.0
        defaultCursor: detachedTerminalWindow.hostRoot.controller.cursorPreference
        cursorBlink: detachedTerminalWindow.hostRoot.controller.cursorBlink
        copyOnSelect: detachedTerminalWindow.hostRoot.controller.copyOnSelect
        keepSelectionAfterCopy: detachedTerminalWindow.hostRoot.controller.keepSelectionAfterCopy
        selectionActionPopupEnabled: detachedTerminalWindow.hostRoot.controller.terminalSelectionPopupEnabled
        selectionActions: detachedTerminalWindow.hostRoot.controller.terminalSelectionActions
        confirmMultilinePaste: detachedTerminalWindow.hostRoot.controller.confirmMultilinePaste
        rightClickBehavior: detachedTerminalWindow.hostRoot.controller.terminalRightClickBehavior
        middleClickBehavior: detachedTerminalWindow.hostRoot.controller.terminalMiddleClickBehavior
        wordDelimiters: detachedTerminalWindow.hostRoot.controller.terminalWordDelimiters
        scrollRowsPerWheel: detachedTerminalWindow.hostRoot.controller.terminalScrollRows
        onDetachPaneRequested: paneId => detachedTerminalWindow.hostRoot.detachTerminalPane(paneId)
        onZoomPaneRequested: paneId => detachedTerminalWindow.hostRoot.toggleTerminalPaneZoom(paneId, detachedTerminalWindow.workspaceId)
        onToggleTabBarRequested: detachedTerminalWindow.toggleTabBar()
        onMultilinePasteConfirmationRequested: (viewport, lineCount) => {
            detachedTerminalWindow.pendingPasteViewport = viewport;
            detachedTerminalWindow.pendingPasteLineCount = lineCount;
            Qt.callLater(() => pasteDialog.openFrom(viewport));
        }
        onTerminalSearchRequested: detachedTerminalWindow.openTerminalSearch()
    }

    TerminalSearchBar {
        id: searchPanel
        objectName: "detachedTerminalSearch"
        controller: detachedTerminalWindow.controller
        workspaceId: detachedTerminalWindow.workspaceId
        windowActive: detachedTerminalWindow.active
        anchors.top: parent.top
        anchors.topMargin: tabBar.height + 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        width: Math.min(420, parent.width - 24)
        onClosed: detachedViewport.forceActiveFocus()
        z: 80
    }

    PaneDragSurface {
        id: paneDrag
        hostRoot: detachedTerminalWindow
        coordinator: detachedTerminalWindow.coordinator
        terminalArea: detachedViewport
    }
    Shortcut {
        sequence: "Escape"
        enabled: paneDrag.dragging
        onActivated: paneDrag.cancelDrag()
    }
    DropTargetIndicator {
        z: 90
        target: detachedTerminalWindow.coordinator.dropTarget.windowId === detachedTerminalWindow.ownerWindowId ? detachedTerminalWindow.coordinator.dropTarget : ({})
    }

    TerminalRenameDialog {
        id: renameDialog
        controller: detachedTerminalWindow.controller
    }

    SavedHostConnection {
        id: savedHostConnection
        anchors.fill: parent
        controller: detachedTerminalWindow.controller
        onConnectionStarting: detachedTerminalWindow.hostMainSelection = detachedTerminalWindow.hostRoot.requestedMainWorkspaceId
        onConnectionStarted: {
            const index = detachedTerminalWindow.tabs.findIndex(tab => tab.id === detachedTerminalWindow.workspaceId) + 1;
            if (controller.insertTerminalWorkspace(controller.activeTerminalTabId, index, detachedTerminalWindow.ownerWindowId)) {
                detachedTerminalWindow.hostRoot.requestedMainWorkspaceId = detachedTerminalWindow.hostMainSelection;
                Qt.callLater(detachedTerminalWindow.hostRoot.refreshMainWorkspace);
            }
        }
        onConnectionError: message => detachedTerminalWindow.hostConnectionError = message
    }
    StatusMessage {
        anchors.top: parent.top
        anchors.topMargin: tabBar.height + 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(440, parent.width - 32)
        text: detachedTerminalWindow.hostConnectionError
        kind: "error"
        z: 90
    }

    TerminalNotificationToast {
        controller: detachedTerminalWindow.controller
        ownerWindowId: detachedTerminalWindow.ownerWindowId
        x: detachedTerminalWindow.width - width - 16
        y: 46
        z: 100
    }

    ConfirmationDialog {
        id: pasteDialog
        heading: qsTr("Paste multiple lines?")
        description: qsTr("Paste %n line(s) into this detached terminal?", "", detachedTerminalWindow.pendingPasteLineCount)
        acceptText: qsTr("Paste")
        onAccepted: {
            if (detachedTerminalWindow.pendingPasteViewport)
                detachedTerminalWindow.pendingPasteViewport.resolveMultilinePaste(true);
            detachedTerminalWindow.pendingPasteViewport = null;
        }
        onRejected: {
            if (detachedTerminalWindow.pendingPasteViewport)
                detachedTerminalWindow.pendingPasteViewport.resolveMultilinePaste(false);
            detachedTerminalWindow.pendingPasteViewport = null;
        }
    }
}
