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
    property bool windowControlsVisible: false
    readonly property int titleTriggerHeight: hostRoot.titleTriggerHeight
    readonly property int windowControlsHeight: Theme.titleBarHeight
    readonly property real windowChromeHeight: windowControlsHeight * windowControls.revealProgress
    readonly property bool windowChromeInteractive: windowControls.revealProgress > 0
    property int titleHoverElapsed: 0
    property int titleLeaveElapsed: 0
    property bool nativeMaximizeButtonHovered: false
    property bool nativeMaximizeButtonPressed: false
    readonly property var controller: hostRoot.controller
    readonly property string currentPage: "terminal"
    readonly property bool maximized: visibility === Window.Maximized
    readonly property var localActionIds: ["terminal.newLocal", "tabs.duplicate", "terminal.find", "tabs.close", "terminal.splitHorizontal", "terminal.splitVertical", "terminal.duplicatePane", "terminal.focusNextPane", "terminal.focusPreviousPane", "terminal.growPane", "terminal.shrinkPane", "terminal.swapNextPane", "terminal.swapPreviousPane"]

    Repeater {
        model: detachedTerminalWindow.controller.actions.filter(action => detachedTerminalWindow.localActionIds.includes(action.id))
        Item {
            id: localShortcut
            required property var modelData
            width: 0
            height: 0
            Shortcut {
                sequence: localShortcut.modelData.shortcut
                enabled: detachedTerminalWindow.active && localShortcut.modelData.enabled && sequence.length > 0
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
        if (!controller.activateTerminalTab(workspaceId))
            return false;
        return controller.splitActiveTerminal("auto", sourceId.length > 0, "", shellId);
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
    function titleInteractionHeld(focused) {
        let keyboardFocus = false;
        while (focused) {
            keyboardFocus = keyboardFocus || focused.visualFocus === true;
            if (focused === windowControls)
                return keyboardFocus;
            focused = focused.parent;
        }
        // Hover is already covered by the native pointer probe. A stale
        // non-client hover notification must not pin the overlay open.
        return nativeMaximizeButtonPressed;
    }
    Timer {
        interval: 50
        repeat: true
        running: detachedTerminalWindow.visible && detachedTerminalWindow.visibility !== Window.Minimized
        onTriggered: {
            const extent = Math.max(detachedTerminalWindow.titleTriggerHeight, detachedTerminalWindow.windowChromeHeight);
            const inside = detachedTerminalWindow.hostRoot.windowChrome.titleBarPointerInside(extent, detachedTerminalWindow);
            if (inside || (detachedTerminalWindow.windowControlsVisible && detachedTerminalWindow.titleInteractionHeld(detachedTerminalWindow.activeFocusItem))) {
                detachedTerminalWindow.titleLeaveElapsed = 0;
                detachedTerminalWindow.titleHoverElapsed += interval;
                if (detachedTerminalWindow.titleHoverElapsed >= 150)
                    detachedTerminalWindow.windowControlsVisible = true;
            } else {
                detachedTerminalWindow.titleHoverElapsed = 0;
                detachedTerminalWindow.titleLeaveElapsed += interval;
                if (detachedTerminalWindow.titleLeaveElapsed >= 300)
                    detachedTerminalWindow.windowControlsVisible = false;
            }
        }
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
    // NativeWindow owns the clear colour; theme tint belongs in the scene,
    // exactly as in Main.qml, rather than being overwritten by applyBackdrop.
    color: "transparent"
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

    Rectangle {
        objectName: "detachedWorkspaceBackground"
        anchors.fill: parent
        color: Theme.workspaceBackground
    }

    Rectangle {
        objectName: "detachedTitleTriggerStrip"
        width: parent.width
        height: detachedTerminalWindow.titleTriggerHeight
        color: Theme.workspaceInputFloor
    }

    MouseArea {
        objectName: "detachedTitleBarInputShield"
        width: parent.width
        height: detachedTerminalWindow.windowControlsHeight
        visible: detachedTerminalWindow.windowChromeInteractive
        z: 90
        acceptedButtons: Qt.AllButtons
        property point pressPosition
        onPressed: mouse => pressPosition = Qt.point(mouse.x, mouse.y)
        onPositionChanged: mouse => {
            if (pressed && (pressedButtons & Qt.LeftButton) && Math.hypot(mouse.x - pressPosition.x, mouse.y - pressPosition.y) >= drag.threshold)
                detachedTerminalWindow.startSystemMove();
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                WindowControl.toggleMaximize(detachedTerminalWindow);
        }
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: windowControls
        objectName: "detachedWindowControls"
        x: 0
        y: 0
        width: parent.width
        height: detachedTerminalWindow.windowControlsHeight
        color: Theme.floatingBackground
        property real revealProgress: detachedTerminalWindow.windowControlsVisible ? 1 : 0
        visible: revealProgress > 0
        z: 91
        transform: Translate {
            y: -windowControls.height * (1 - windowControls.revealProgress)
        }
        enabled: visible
        onVisibleChanged: {
            if (!visible) {
                detachedTerminalWindow.nativeMaximizeButtonHovered = false;
                detachedTerminalWindow.nativeMaximizeButtonPressed = false;
            }
        }
        Behavior on revealProgress {
            NumberAnimation {
                duration: Motion.reduced ? 0 : detachedTerminalWindow.windowControlsVisible ? Motion.enter : Motion.exit
                easing.type: detachedTerminalWindow.windowControlsVisible ? Motion.enterEasing : Motion.exitEasing
            }
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - 160)
            text: detachedTerminalWindow.workspace.title || qsTr("Terminal")
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
            color: Theme.text
            elide: Text.ElideRight
        }
        TitleChromeAction {
            objectName: "detachedReattachAllButton"
            x: parent.width - 128
            width: 32
            height: parent.height
            iconName: "chevron-left"
            accessibleName: qsTr("Reattach window to main window")
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
                    height: windowControls.height
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
        anchors.topMargin: detachedTerminalWindow.titleTriggerHeight
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
        onDetachPaneRequested: paneId => detachedTerminalWindow.coordinator.reattachPane(detachedTerminalWindow, paneId)
        onZoomPaneRequested: paneId => detachedTerminalWindow.hostRoot.toggleTerminalPaneZoom(paneId, detachedTerminalWindow.workspaceId)
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
        anchors.topMargin: 12
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
