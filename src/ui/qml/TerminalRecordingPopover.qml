pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popover

    required property var controller
    property var terminalTab: null

    function openFor(item) {
        if (terminalTab === null)
            return;
        const overlay = Overlay.overlay;
        const point = item.mapToItem(overlay, item.width - width, item.height + 6);
        const targetX = Math.max(8, Math.min(point.x, overlay.width - width - 8));
        const targetY = Math.max(8, Math.min(point.y, overlay.height - height - 8));
        const localPoint = overlay.mapToItem(item, targetX, targetY);
        parent = item;
        x = localPoint.x;
        y = localPoint.y;
        open();
    }

    width: 420
    height: Math.min(480, contentColumn.implicitHeight + 28)
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.floatingBackground
        border.color: Theme.borderStrong
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: Theme.danger
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Recorded command actions")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }
            Text {
                text: popover.terminalTab !== null ? qsTr("%1 step(s)").arg(popover.terminalTab.scriptRecordingSteps.length) : ""
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Only commands run from the composer, history, or command snippets are captured. Raw keyboard input and password prompts are never recorded.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(270, stepsColumn.implicitHeight)
            clip: true

            Column {
                id: stepsColumn
                width: parent.width
                spacing: 3

                Repeater {
                    model: popover.terminalTab !== null ? popover.terminalTab.scriptRecordingSteps : []
                    delegate: Rectangle {
                        id: stepDelegate

                        required property var modelData
                        width: stepsColumn.width
                        height: stepDelegate.modelData.type === "send" ? Math.max(34, commandText.implicitHeight + 12) : 28
                        radius: Theme.radiusSmall
                        color: stepDelegate.modelData.type === "send" ? Theme.controlBackground : "transparent"

                        Text {
                            id: commandText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 8
                            text: stepDelegate.modelData.type === "send" ? stepDelegate.modelData.command : qsTr("Wait %1 ms").arg(stepDelegate.modelData.milliseconds)
                            color: stepDelegate.modelData.type === "send" ? Theme.text : Theme.textMuted
                            wrapMode: Text.WrapAnywhere
                            font.family: stepDelegate.modelData.type === "send" ? Theme.terminalFont : Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: qsTr("Clear")
                onClicked: {
                    popover.controller.clearTerminalScriptRecording();
                    popover.close();
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: qsTr("Copy JSON")
                enabled: popover.terminalTab !== null && popover.terminalTab.scriptRecordingSteps.length > 0
                onClicked: popover.controller.copyTerminalScriptRecording()
            }
            ActionButton {
                text: popover.terminalTab !== null && popover.terminalTab.scriptPlaybackActive ? qsTr("Running…") : qsTr("Replay")
                variant: "primary"
                enabled: popover.terminalTab !== null && popover.terminalTab.scriptRecordingState === "review" && popover.terminalTab.scriptRecordingSteps.length > 0 && !popover.terminalTab.scriptPlaybackActive
                onClicked: popover.controller.replayTerminalScriptRecording()
            }
        }
    }
}
