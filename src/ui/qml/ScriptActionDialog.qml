pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var controller
    required property var activeTab
    property string ownerSessionId: ""
    property bool waitsForOutput: false
    property bool resolving: false
    property Item focusRestoreItem: null
    signal insertRequested(string command)

    function openFor(script, execute, focusItem) {
        if (visible || !activeTab || !activeTab.running)
            return false;
        ownerSessionId = activeTab.sessionId;
        resolving = false;
        focusRestoreItem = focusItem;
        waitsForOutput = !execute && script.steps.some(step => step.continuation !== "immediate");
        runPane.insertOnly = !execute && !waitsForOutput;
        runPane.begin(script);
        runPane.targetSessionId = ownerSessionId;
        open();
        return true;
    }

    onActiveTabChanged: {
        if (visible && (!activeTab || activeTab.sessionId !== ownerSessionId))
            close();
    }
    objectName: "scriptActionDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, Math.max(0, parent.width - 48))
    height: Math.min(660, Math.max(0, parent.height - 48), runPane.implicitHeight + waitStatus.implicitHeight + actionLayout.spacing + topPadding + bottomPadding)
    padding: 16
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    enter: MotionEnter {}
    exit: MotionExit {}
    onAboutToHide: resolving = true
    onClosed: {
        const restoreItem = focusRestoreItem;
        focusRestoreItem = null;
        ownerSessionId = "";
        if (restoreItem && restoreItem.visible && restoreItem.enabled)
            Qt.callLater(() => restoreItem.forceActiveFocus());
    }
    Overlay.modal: Rectangle {
        color: Theme.modalScrim
    }
    background: AppSurface {
        elevation: 3
    }
    contentItem: ColumnLayout {
        id: actionLayout
        spacing: 8
        StatusMessage {
            id: waitStatus
            Layout.fillWidth: true
            text: dialog.waitsForOutput ? qsTr("This script waits for output. Use Run to preserve its steps.") : ""
        }
        ScriptRunPane {
            id: runPane
            objectName: "scriptActionRunPane"
            Layout.fillWidth: true
            Layout.fillHeight: true
            controller: dialog.controller
            activeTab: dialog.activeTab
            targetLocked: true
            enabled: !dialog.resolving
            onClosed: dialog.close()
            onInsertRequested: command => {
                if (dialog.activeTab && dialog.activeTab.sessionId === dialog.ownerSessionId)
                    dialog.insertRequested(command);
            }
            onSubmitted: dialog.close()
        }
    }
}
