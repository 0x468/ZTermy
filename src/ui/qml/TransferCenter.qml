pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: center

    required property var controller
    property string conflictTaskId: ""
    property var conflict: ({})
    readonly property bool hasFinishedTasks: {
        for (const task of controller.transferTasks) {
            if (task.status === "completed" || task.status === "failed" || task.status === "cancelled") {
                return true;
            }
        }
        return false;
    }

    width: 440
    height: Math.min(520, Math.max(170, contentColumn.implicitHeight + topPadding + bottomPadding))
    padding: 12
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function formatBytes(bytes) {
        if (bytes < 1024) {
            return qsTr("%1 B").arg(bytes);
        }
        if (bytes < 1024 * 1024) {
            return qsTr("%1 KB").arg((bytes / 1024).toFixed(1));
        }
        if (bytes < 1024 * 1024 * 1024) {
            return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1));
        }
        return qsTr("%1 GB").arg((bytes / (1024 * 1024 * 1024)).toFixed(1));
    }

    function statusText(task) {
        switch (task.status) {
        case "queued":
            return qsTr("Queued");
        case "running":
            return task.totalBytes > 0 ? qsTr("%1% · %2 of %3").arg(Math.min(100, Math.floor(task.transferredBytes * 100 / task.totalBytes))).arg(formatBytes(task.transferredBytes)).arg(formatBytes(task.totalBytes)) : qsTr("Transferring");
        case "cancelling":
            return qsTr("Cancelling…");
        case "needs-attention":
            return task.errorMessage || qsTr("Needs attention");
        case "completed":
            return qsTr("Completed");
        case "cancelled":
            return qsTr("Cancelled");
        case "failed":
        default:
            return task.errorMessage || qsTr("Failed");
        }
    }

    function resolveConflict(action) {
        controller.resolveTransferConflict(conflictTaskId, action, action === "rename" ? conflictRename.text : "");
        conflictDialog.close();
    }

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.floatingBackground
        border.color: Theme.borderStrong
    }

    contentItem: ColumnLayout {
        id: contentColumn

        spacing: 8
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("File transfers")

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppIcon {
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
                name: "transfer"
                color: Theme.accent
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("File transfers")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody + 2
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("%n active", "", center.controller.activeTransferCount)
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }

            ToolButton {
                id: clearFinishedButton

                visible: center.hasFinishedTasks
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                onClicked: center.controller.clearFinishedTransfers()
                Accessible.name: qsTr("Clear finished transfers")
                contentItem: AppIcon {
                    name: "trash"
                    color: Theme.textMuted
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: clearFinishedButton.hovered ? Theme.controlHover : "transparent"
                    border.color: clearFinishedButton.activeFocus ? Theme.focus : "transparent"
                    border.width: clearFinishedButton.activeFocus ? 2 : 0
                }
                AppToolTip {
                    text: qsTr("Clear finished transfers")
                }
            }
        }

        ListView {
            id: transferList

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(420, contentHeight)
            Layout.minimumHeight: count > 0 ? Math.min(80, contentHeight) : 0
            visible: count > 0
            clip: true
            spacing: 3
            model: center.controller.transferTasks
            activeFocusOnTab: true
            keyNavigationEnabled: true

            delegate: Rectangle {
                id: taskDelegate

                required property var modelData
                required property int index

                readonly property bool cancellable: modelData.status === "queued" || modelData.status === "running"
                readonly property bool retryable: modelData.retryable && (modelData.status === "failed" || modelData.status === "needs-attention")
                readonly property bool dismissible: modelData.status === "completed" || modelData.status === "failed" || modelData.status === "cancelled"

                width: ListView.view.width
                height: 72
                radius: Theme.radiusSmall
                color: ListView.isCurrentItem ? Theme.selectedBackground : taskHover.hovered ? Theme.controlHover : "transparent"
                border.color: activeFocus ? Theme.focus : "transparent"
                focus: ListView.isCurrentItem
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2").arg(modelData.displayName).arg(center.statusText(modelData))

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 5
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        name: taskDelegate.modelData.direction === "upload" ? "upload" : "download"
                        color: taskDelegate.modelData.status === "failed" || taskDelegate.modelData.status === "needs-attention" ? Theme.dangerText : Theme.accent
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            Layout.fillWidth: true
                            text: taskDelegate.modelData.displayName
                            color: Theme.text
                            elide: Text.ElideMiddle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: center.statusText(taskDelegate.modelData)
                            color: taskDelegate.modelData.status === "failed" || taskDelegate.modelData.status === "needs-attention" ? Theme.dangerText : Theme.textMuted
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            visible: taskDelegate.modelData.status === "running" || taskDelegate.modelData.status === "cancelling"
                            from: 0
                            to: Math.max(1, taskDelegate.modelData.totalBytes)
                            value: taskDelegate.modelData.transferredBytes
                            indeterminate: taskDelegate.modelData.totalBytes === 0
                            Accessible.name: qsTr("Transfer progress")
                        }
                    }

                    ToolButton {
                        id: retryButton

                        visible: taskDelegate.retryable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: center.controller.retryTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Retry %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "refresh"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: retryButton.down ? Theme.controlPressed : retryButton.hovered ? Theme.controlHover : "transparent"
                            border.color: retryButton.activeFocus ? Theme.focus : "transparent"
                            border.width: retryButton.activeFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Retry")
                        }
                    }

                    ToolButton {
                        id: cancelButton

                        visible: taskDelegate.cancellable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: center.controller.cancelTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Cancel %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: cancelButton.down ? Theme.controlPressed : cancelButton.hovered ? Theme.controlHover : "transparent"
                            border.color: cancelButton.activeFocus ? Theme.focus : "transparent"
                            border.width: cancelButton.activeFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Cancel")
                        }
                    }

                    ToolButton {
                        id: dismissButton

                        visible: taskDelegate.dismissible
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: center.controller.dismissTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Remove %1 from transfer history").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: dismissButton.down ? Theme.controlPressed : dismissButton.hovered ? Theme.controlHover : "transparent"
                            border.color: dismissButton.activeFocus ? Theme.focus : "transparent"
                            border.width: dismissButton.activeFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Remove")
                        }
                    }
                }
                HoverHandler {
                    id: taskHover
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            visible: center.controller.transferTasks.length === 0
            kind: "empty"
            centered: true
            heading: qsTr("No file transfers")
            description: qsTr("Uploads and downloads from SFTP will appear here.")
        }
    }

    Connections {
        target: center.controller

        function onTransferConflictRequested(taskId, details) {
            center.conflictTaskId = taskId;
            center.conflict = details;
            conflictRename.text = details.destinationPath || "";
            center.open();
            conflictDialog.open();
        }
    }

    Dialog {
        id: conflictDialog

        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        padding: 16
        title: qsTr("File already exists")
        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.preferredWidth: 420
                text: qsTr("Choose how to handle the existing destination:\n%1").arg(center.conflict.destinationPath || "")
                color: Theme.text
                wrapMode: Text.WrapAnywhere
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            AppTextField {
                id: conflictRename

                Layout.fillWidth: true
                placeholderText: qsTr("Renamed destination path")
                accessibleName: qsTr("Renamed destination path")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ActionButton {
                    text: qsTr("Skip")
                    onClicked: center.resolveConflict("skip")
                }
                ActionButton {
                    text: qsTr("Cancel")
                    onClicked: center.resolveConflict("cancel")
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Rename")
                    enabled: conflictRename.text.trim().length > 0
                    onClicked: center.resolveConflict("rename")
                }
                ActionButton {
                    text: qsTr("Replace")
                    variant: "primary"
                    onClicked: center.resolveConflict("replace")
                }
            }
        }
    }
}
