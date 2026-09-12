pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: detachedTerminalWindow
    objectName: "detachedTerminalWindow"
    required property var hostRoot
    property var pendingPasteViewport: null
    property int pendingPasteLineCount: 0

    transientParent: null
    flags: Qt.Window
    width: 920
    height: 620
    minimumWidth: 480
    minimumHeight: 320
    visible: false
    title: qsTr("%1 — Detached pane").arg(detachedTerminalWindow.hostRoot.detachedTerminalWorkspace.title || qsTr("Terminal"))
    color: Theme.windowBackground
    onClosing: close => {
        if (detachedTerminalWindow.hostRoot.detachedTerminalPaneId.length > 0) {
            close.accepted = false;
            detachedTerminalWindow.hostRoot.reattachTerminalPane();
        }
    }

    TerminalSplitNode {
        anchors.fill: parent
        controller: detachedTerminalWindow.hostRoot.controller
        node: detachedTerminalWindow.hostRoot.findTerminalPane(detachedTerminalWindow.hostRoot.detachedTerminalWorkspace.root, detachedTerminalWindow.hostRoot.detachedTerminalPaneId) || ({})
        detachedPane: true
        defaultFontFamily: detachedTerminalWindow.hostRoot.controller.terminalFontFamily
        defaultFontSize: detachedTerminalWindow.hostRoot.controller.terminalFontSize
        defaultLigatures: detachedTerminalWindow.hostRoot.controller.terminalLigatures
        defaultBackgroundOpacity: detachedTerminalWindow.hostRoot.controller.terminalBackgroundOpacity
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
        onDetachPaneRequested: paneId => detachedTerminalWindow.hostRoot.reattachTerminalPane()
        onZoomPaneRequested: paneId => {}
        onMultilinePasteConfirmationRequested: (viewport, lineCount) => {
            detachedTerminalWindow.pendingPasteViewport = viewport;
            detachedTerminalWindow.pendingPasteLineCount = lineCount;
            Qt.callLater(() => pasteDialog.openFrom(viewport));
        }
        onTerminalSearchRequested: detachedTerminalWindow.hostRoot.openTerminalSearch()
        onBrowseHostsRequested: {
            detachedTerminalWindow.hostRoot.reattachTerminalPane();
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
