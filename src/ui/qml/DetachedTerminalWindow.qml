pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: detachedTerminalWindow
    objectName: "detachedTerminalWindow"
    required property var hostRoot
    property string workspaceId: ""
    property var workspace: ({})
    property var pendingPasteViewport: null
    property int pendingPasteLineCount: 0
    property bool paneHeadersVisible: true

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
        Qt.callLater(() => hostRoot.controller.closeTerminalTab(workspaceId));
    }

    TerminalSplitNode {
        id: detachedViewport
        anchors.fill: parent
        controller: detachedTerminalWindow.hostRoot.controller
        node: detachedTerminalWindow.workspace.root || ({})
        paneCount: detachedTerminalWindow.workspace.paneCount || 1
        headersVisible: detachedTerminalWindow.paneHeadersVisible
        detachedPane: true
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
