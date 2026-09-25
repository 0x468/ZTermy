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
    property var pendingPasteViewport: null
    property int pendingPasteLineCount: 0
    property bool paneHeadersVisible: false
    property bool paneDockMoveActive: false
    property bool nativeMaximizeButtonHovered: false
    property bool nativeMaximizeButtonPressed: false
    readonly property var controller: hostRoot.controller
    readonly property string currentPage: "terminal"
    readonly property bool maximized: visibility === Window.Maximized

    function selectWorkspace(id) {
        if (!tabs.some(tab => tab.id === id))
            return;
        workspaceId = id;
        controller.activateTerminalTab(id);
        workspace = controller.terminalWorkspace(id);
    }
    function toggleTerminalPaneHeaders() {
        paneHeadersVisible = !paneHeadersVisible;
    }
    function focusTerminalAfterLayout() {
        controller.activateTerminalTab(workspaceId);
    }
    function closeTab(id) {
        const other = tabs.find(tab => tab.id !== id);
        controller.closeTerminalTab(id, other ? other.id : hostRoot.requestedMainWorkspaceId);
    }
    function tabInsertionIndex(globalPosition) {
        const p = tabRow.mapFromGlobal(globalPosition.x, globalPosition.y);
        return Math.max(0, Math.min(tabs.length, Math.floor((p.x + 80) / 160)));
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
    onActiveChanged: {
        if (active && workspaceId.length > 0)
            hostRoot.controller.activateTerminalTab(workspaceId);
    }
    title: qsTr("%1 — Detached pane").arg(workspace.title || qsTr("Terminal"))
    color: Theme.workspaceBackground
    onClosing: close => {
        close.accepted = false;
        const ids = tabs.map(tab => tab.id);
        Qt.callLater(() => {
            for (const id of ids)
                controller.closeTerminalTab(id, hostRoot.requestedMainWorkspaceId);
        });
    }

    Item {
        id: tabBar
        width: parent.width
        height: 32
        MouseArea {
            anchors.fill: parent
            onPressed: {
                detachedTerminalWindow.paneDockMoveActive = true;
                if (!detachedTerminalWindow.startSystemMove())
                    detachedTerminalWindow.paneDockMoveActive = false;
            }
            onDoubleClicked: WindowControl.toggleMaximize(detachedTerminalWindow)
        }
        Flickable {
            id: tabScroll
            width: Math.max(0, parent.width - 160)
            height: parent.height
            contentWidth: tabRow.width
            clip: true
            flickableDirection: Flickable.HorizontalFlick
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
                        iconName: "terminal"
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
                        onDuplicateRequested: {
                            if (detachedTerminalWindow.controller.duplicateTerminalTab(modelData.id))
                                detachedTerminalWindow.controller.insertTerminalWorkspace(detachedTerminalWindow.controller.activeTerminalTabId, modelData.tabIndex + 1, detachedTerminalWindow.ownerWindowId);
                        }
                        onMoveLeftRequested: detachedTerminalWindow.controller.moveTerminalTab(modelData.id, modelData.tabIndex - 1)
                        onMoveRightRequested: detachedTerminalWindow.controller.moveTerminalTab(modelData.id, modelData.tabIndex + 1)
                        onCloseOthersRequested: detachedTerminalWindow.controller.closeOtherTerminalTabs(modelData.id)
                        onCloseToRightRequested: detachedTerminalWindow.controller.closeTerminalTabsToRight(modelData.id)
                    }
                }
            }
        }
        TitleChromeAction {
            x: tabScroll.width
            width: 32
            height: 32
            iconName: "plus"
            accessibleName: qsTranslate("Main", "New terminal")
            toolTip: accessibleName
            onActivated: {
                const id = detachedTerminalWindow.controller.startLocalTerminal();
                if (id)
                    detachedTerminalWindow.controller.insertTerminalWorkspace(id, detachedTerminalWindow.tabs.length, detachedTerminalWindow.ownerWindowId);
            }
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
        anchors.fill: parent
        anchors.topMargin: tabBar.height
        controller: detachedTerminalWindow.hostRoot.controller
        node: detachedTerminalWindow.workspace.root || ({})
        paneCount: detachedTerminalWindow.workspace.paneCount || 1
        headersVisible: detachedTerminalWindow.paneHeadersVisible
        detachedPane: false
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
        onDetachPaneRequested: paneId => {
            if (paneId.length > 0)
                detachedTerminalWindow.hostRoot.detachTerminalPane(paneId);
            else
                detachedTerminalWindow.hostRoot.reattachWorkspace(detachedTerminalWindow.workspaceId);
        }
        onZoomPaneRequested: paneId => {}
        onToggleHeadersRequested: detachedTerminalWindow.paneHeadersVisible = !detachedTerminalWindow.paneHeadersVisible
        onMultilinePasteConfirmationRequested: (viewport, lineCount) => {
            detachedTerminalWindow.pendingPasteViewport = viewport;
            detachedTerminalWindow.pendingPasteLineCount = lineCount;
            Qt.callLater(() => pasteDialog.openFrom(viewport));
        }
        onTerminalSearchRequested: detachedTerminalWindow.hostRoot.openTerminalSearch()
        onBrowseHostsRequested: {
            detachedTerminalWindow.hostRoot.reattachWorkspace(detachedTerminalWindow.workspaceId);
            detachedTerminalWindow.hostRoot.currentPage = "hosts";
        }
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
    DragPreview {
        z: 91
        visible: paneDrag.dragging
        x: paneDrag.pointerPoint.x + 16
        y: paneDrag.pointerPoint.y + 18
        title: paneDrag.paneTitle
    }
    DropTargetIndicator {
        z: 90
        target: detachedTerminalWindow.coordinator.dropTarget.windowId === detachedTerminalWindow.ownerWindowId ? detachedTerminalWindow.coordinator.dropTarget : ({})
    }

    TerminalRenameDialog {
        id: renameDialog
        controller: detachedTerminalWindow.controller
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
