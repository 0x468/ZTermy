pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: center

    objectName: "transferCenter"

    required property var controller
    property string conflictTaskId: ""
    property var conflict: ({})
    readonly property bool hasFinishedTasks: {
        for (const batch of controller.transferBatches) {
            if (batch.status === "completed" || batch.status === "failed" || batch.status === "cancelled" || batch.status === "interrupted") {
                return true;
            }
        }
        for (const task of controller.transferTasks) {
            if (task.status === "completed" || task.status === "failed" || task.status === "cancelled") {
                return true;
            }
        }
        return false;
    }
    readonly property bool hasPausableTasks: controller.transferTasks.some(task => task.status === "queued" || task.status === "running") || controller.transferBatches.some(batch => batch.status === "discovering" || batch.status === "ready" || batch.status === "running")
    readonly property bool hasPausedTasks: controller.transferTasks.some(task => task.status === "paused") || controller.transferBatches.some(batch => batch.status === "paused")
    readonly property bool completedDownloadDragAvailable: controller.transferTasks.some(task => task.direction === "download" && task.status === "completed")
    readonly property bool hasActiveTasks: controller.transferTasks.some(task => task.status === "queued" || task.status === "running" || task.status === "pausing" || task.status === "paused" || task.status === "needs-attention") || controller.transferBatches.some(batch => batch.status === "discovering" || batch.status === "ready" || batch.status === "running" || batch.status === "paused" || batch.status === "needs-attention" || batch.status === "interrupted")

    width: 500
    height: Math.min(620, Math.max(170, contentColumn.implicitHeight + topPadding + bottomPadding))
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
        case "pausing":
            return qsTr("Pausing…");
        case "paused":
            return task.totalBytes > 0 ? qsTr("Paused · %1 of %2").arg(formatBytes(task.transferredBytes)).arg(formatBytes(task.totalBytes)) : qsTr("Paused");
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

    function batchStatusText(batch) {
        switch (batch.status) {
        case "discovering":
            return qsTr("Discovering files…");
        case "ready":
            return qsTr("Preparing transfer…");
        case "running":
            if (batch.fileCount === 0) {
                return qsTr("Transferring folders…");
            }
            if (batch.bytesPerSecond > 0 && batch.totalBytes > batch.transferredBytes) {
                const remainingSeconds = Math.ceil((batch.totalBytes - batch.transferredBytes) / batch.bytesPerSecond);
                return qsTr("%1 of %2 files · %3 of %4 · %5/s · %6 left").arg(batch.completedCount).arg(batch.fileCount).arg(formatBytes(batch.transferredBytes)).arg(formatBytes(batch.totalBytes)).arg(formatBytes(batch.bytesPerSecond)).arg(formatDuration(remainingSeconds));
            }
            return qsTr("%1 of %2 files · %3 of %4").arg(batch.completedCount).arg(batch.fileCount).arg(formatBytes(batch.transferredBytes)).arg(formatBytes(batch.totalBytes));
        case "paused":
            return qsTr("Paused · %1 of %2 files").arg(batch.completedCount).arg(batch.fileCount);
        case "needs-attention":
            return batch.errorMessage || qsTr("A file needs attention");
        case "completed":
            return qsTr("Completed · %1 files, %2 folders").arg(batch.fileCount).arg(batch.directoryCount);
        case "cancelled":
            return qsTr("Cancelled");
        case "interrupted":
            return qsTr("Interrupted · retry to continue");
        case "failed":
        default:
            return batch.errorMessage || qsTr("Failed");
        }
    }

    function formatDuration(seconds) {
        if (seconds < 60) {
            return qsTr("%1s").arg(seconds);
        }
        if (seconds < 3600) {
            return qsTr("%1m %2s").arg(Math.floor(seconds / 60)).arg(seconds % 60);
        }
        return qsTr("%1h %2m").arg(Math.floor(seconds / 3600)).arg(Math.floor((seconds % 3600) / 60));
    }

    function resolveConflict(action) {
        controller.resolveTransferConflict(conflictTaskId, action, action === "rename" ? conflictRename.text : "", conflictApplyRemaining.checked && (action === "skip" || action === "replace"));
        conflictDialog.close();
    }

    function localFileUrl(path) {
        return "file:///" + encodeURI(path.replace(/\\/g, "/"));
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

            ToolButton {
                id: pauseAllButton

                objectName: "transferPauseAllButton"

                visible: center.hasPausableTasks
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                onClicked: center.controller.pauseAllTransfers()
                Accessible.name: qsTr("Pause all transfers")
                contentItem: AppIcon {
                    name: "pause"
                    color: Theme.textMuted
                }
                background: Rectangle {
                    radius: width / 2
                    color: pauseAllButton.hovered ? Theme.controlHover : "transparent"
                }
                AppToolTip {
                    text: qsTr("Pause all")
                }
            }

            ToolButton {
                id: resumeAllButton

                objectName: "transferResumeAllButton"

                visible: center.hasPausedTasks
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                onClicked: center.controller.resumeAllTransfers()
                Accessible.name: qsTr("Resume all transfers")
                contentItem: AppIcon {
                    name: "play"
                    color: Theme.textMuted
                }
                background: Rectangle {
                    radius: width / 2
                    color: resumeAllButton.hovered ? Theme.controlHover : "transparent"
                }
                AppToolTip {
                    text: qsTr("Resume all")
                }
            }

            ToolButton {
                id: cancelAllButton

                objectName: "transferCancelAllButton"

                visible: center.hasActiveTasks
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                onClicked: center.controller.cancelAllTransfers()
                Accessible.name: qsTr("Cancel all transfers")
                contentItem: AppIcon {
                    name: "close"
                    color: Theme.textMuted
                }
                background: Rectangle {
                    radius: width / 2
                    color: cancelAllButton.hovered ? Theme.controlHover : "transparent"
                }
                AppToolTip {
                    text: qsTr("Cancel all")
                }
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
                    border.color: clearFinishedButton.visualFocus ? Theme.focus : "transparent"
                    border.width: clearFinishedButton.visualFocus ? 2 : 0
                }
                AppToolTip {
                    text: qsTr("Clear finished transfers")
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: batchList.count > 0
            text: qsTr("Batches")
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
            font.weight: Font.DemiBold
        }

        ListView {
            id: batchList

            objectName: "transferBatchList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(210, contentHeight)
            Layout.minimumHeight: count > 0 ? Math.min(96, contentHeight) : 0
            visible: count > 0
            clip: true
            spacing: 4
            model: center.controller.transferBatches
            activeFocusOnTab: true
            keyNavigationEnabled: true

            delegate: Rectangle {
                id: batchDelegate

                required property var modelData
                required property int index

                readonly property bool pausable: modelData.status === "discovering" || modelData.status === "ready" || modelData.status === "running"
                readonly property bool resumable: modelData.status === "paused"
                readonly property bool cancellable: pausable || resumable || modelData.status === "needs-attention" || modelData.status === "interrupted"
                readonly property bool retryable: modelData.status === "failed" || modelData.status === "needs-attention" || modelData.status === "interrupted"
                readonly property bool dismissible: modelData.status === "completed" || modelData.status === "failed" || modelData.status === "cancelled"

                width: ListView.view.width
                height: 88
                radius: Theme.radiusControl
                color: batchHover.hovered ? Theme.controlHover : Theme.controlBackground
                border.color: activeFocus ? Theme.focus : Theme.border
                focus: ListView.isCurrentItem
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2").arg(modelData.displayName).arg(center.batchStatusText(modelData))

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        name: batchDelegate.modelData.direction === "upload" ? "upload" : "download"
                        color: batchDelegate.modelData.status === "failed" || batchDelegate.modelData.status === "needs-attention" ? Theme.dangerText : Theme.accent
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: batchDelegate.modelData.displayName
                            color: Theme.text
                            elide: Text.ElideMiddle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: center.batchStatusText(batchDelegate.modelData)
                            color: batchDelegate.modelData.status === "failed" || batchDelegate.modelData.status === "needs-attention" ? Theme.dangerText : Theme.textMuted
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            visible: batchDelegate.modelData.status === "discovering" || batchDelegate.modelData.status === "ready" || batchDelegate.modelData.status === "running" || batchDelegate.modelData.status === "paused"
                            from: 0
                            to: Math.max(1, batchDelegate.modelData.totalBytes > 0 ? batchDelegate.modelData.totalBytes : batchDelegate.modelData.fileCount)
                            value: batchDelegate.modelData.totalBytes > 0 ? batchDelegate.modelData.transferredBytes : batchDelegate.modelData.completedCount
                            indeterminate: batchDelegate.modelData.status === "discovering" || batchDelegate.modelData.status === "ready"
                        }
                    }

                    ToolButton {
                        id: batchPauseButton
                        visible: batchDelegate.pausable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onPressed: center.controller.pauseTransferBatch(batchDelegate.modelData.id)
                        Accessible.name: qsTr("Pause batch")
                        contentItem: AppIcon {
                            name: "pause"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: batchPauseButton.down ? Theme.controlPressed : batchPauseButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Pause batch")
                        }
                    }
                    ToolButton {
                        id: batchResumeButton
                        visible: batchDelegate.resumable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.resumeTransferBatch(batchDelegate.modelData.id)
                        Accessible.name: qsTr("Resume batch")
                        contentItem: AppIcon {
                            name: "play"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: batchResumeButton.down ? Theme.controlPressed : batchResumeButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Resume batch")
                        }
                    }
                    ToolButton {
                        id: batchRetryButton
                        visible: batchDelegate.retryable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.retryTransferBatch(batchDelegate.modelData.id)
                        Accessible.name: qsTr("Retry batch")
                        contentItem: AppIcon {
                            name: "refresh"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: batchRetryButton.down ? Theme.controlPressed : batchRetryButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Retry batch")
                        }
                    }
                    ToolButton {
                        id: batchCancelButton
                        visible: batchDelegate.cancellable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onPressed: center.controller.cancelTransferBatch(batchDelegate.modelData.id)
                        Accessible.name: qsTr("Cancel batch")
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: batchCancelButton.down ? Theme.controlPressed : batchCancelButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Cancel batch")
                        }
                    }
                    ToolButton {
                        id: batchDismissButton
                        visible: batchDelegate.dismissible
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.dismissTransferBatch(batchDelegate.modelData.id)
                        Accessible.name: qsTr("Remove batch")
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: batchDismissButton.down ? Theme.controlPressed : batchDismissButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Remove batch")
                        }
                    }
                }

                HoverHandler {
                    id: batchHover
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        Text {
            Layout.fillWidth: true
            visible: transferList.count > 0 && batchList.count > 0
            text: qsTr("Files")
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
            font.weight: Font.DemiBold
        }

        ListView {
            id: transferList

            objectName: "transferList"

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

                objectName: "transferTask_" + modelData.id

                required property var modelData
                required property int index

                readonly property bool cancellable: modelData.status === "queued" || modelData.status === "running"
                readonly property bool pausable: modelData.status === "queued" || modelData.status === "running"
                readonly property bool resumable: modelData.status === "paused"
                readonly property bool retryable: modelData.retryable && (modelData.status === "failed" || modelData.status === "needs-attention")
                readonly property bool dismissible: modelData.status === "completed" || modelData.status === "failed" || modelData.status === "cancelled"

                width: ListView.view.width
                height: 78
                radius: Theme.radiusSmall
                color: ListView.isCurrentItem ? Theme.selectedBackground : taskHover.hovered ? Theme.controlHover : "transparent"
                border.color: activeFocus ? Theme.focus : "transparent"
                focus: ListView.isCurrentItem
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2").arg(modelData.displayName).arg(center.statusText(modelData))
                Drag.active: completedFileDrag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction
                Drag.mimeData: ({
                        "text/uri-list": center.localFileUrl(modelData.destinationPath)
                    })

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
                            visible: taskDelegate.modelData.status === "running" || taskDelegate.modelData.status === "pausing" || taskDelegate.modelData.status === "paused" || taskDelegate.modelData.status === "cancelling"
                            from: 0
                            to: Math.max(1, taskDelegate.modelData.totalBytes)
                            value: taskDelegate.modelData.transferredBytes
                            indeterminate: taskDelegate.modelData.totalBytes === 0
                            Accessible.name: qsTr("Transfer progress")
                        }
                    }

                    ToolButton {
                        id: pauseButton

                        objectName: "transferPause_" + taskDelegate.modelData.id

                        visible: taskDelegate.pausable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onPressed: center.controller.pauseTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Pause %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "pause"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: pauseButton.down ? Theme.controlPressed : pauseButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Pause")
                        }
                    }

                    ToolButton {
                        id: resumeButton

                        objectName: "transferResume_" + taskDelegate.modelData.id

                        visible: taskDelegate.resumable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.resumeTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Resume %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "play"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: resumeButton.down ? Theme.controlPressed : resumeButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Resume")
                        }
                    }

                    ToolButton {
                        id: retryButton

                        objectName: "transferRetry_" + taskDelegate.modelData.id

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
                            border.color: retryButton.visualFocus ? Theme.focus : "transparent"
                            border.width: retryButton.visualFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Retry")
                        }
                    }

                    ToolButton {
                        id: cancelButton

                        objectName: "transferCancel_" + taskDelegate.modelData.id

                        visible: taskDelegate.cancellable
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        // Progress snapshots can replace this delegate before a mouse release.
                        // Request cancellation on press so the active worker always receives it.
                        onPressed: center.controller.cancelTransfer(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Cancel %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: cancelButton.down ? Theme.controlPressed : cancelButton.hovered ? Theme.controlHover : "transparent"
                            border.color: cancelButton.visualFocus ? Theme.focus : "transparent"
                            border.width: cancelButton.visualFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Cancel")
                        }
                    }

                    ToolButton {
                        id: dismissButton

                        objectName: "transferDismiss_" + taskDelegate.modelData.id

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
                            border.color: dismissButton.visualFocus ? Theme.focus : "transparent"
                            border.width: dismissButton.visualFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Remove")
                        }
                    }

                    ToolButton {
                        id: copyPathButton

                        objectName: "transferCopyPath_" + taskDelegate.modelData.id

                        visible: taskDelegate.dismissible
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.copyTransferPath(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Copy target path for %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "copy"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: copyPathButton.down ? Theme.controlPressed : copyPathButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Copy target path")
                        }
                    }

                    ToolButton {
                        id: openTargetButton

                        objectName: "transferOpenTarget_" + taskDelegate.modelData.id

                        visible: taskDelegate.modelData.direction === "download" && taskDelegate.modelData.status === "completed"
                        Layout.preferredWidth: visible ? 30 : 0
                        Layout.preferredHeight: 30
                        hoverEnabled: true
                        onClicked: center.controller.openTransferTarget(taskDelegate.modelData.id)
                        Accessible.name: qsTr("Open target folder for %1").arg(taskDelegate.modelData.displayName)
                        contentItem: AppIcon {
                            name: "folder"
                            color: Theme.textSoft
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: openTargetButton.down ? Theme.controlPressed : openTargetButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Open target folder")
                        }
                    }
                }
                HoverHandler {
                    id: taskHover
                }

                DragHandler {
                    id: completedFileDrag

                    objectName: "transferDrag_" + taskDelegate.modelData.id

                    enabled: taskDelegate.modelData.direction === "download" && taskDelegate.modelData.status === "completed"
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            visible: center.controller.transferTasks.length === 0 && center.controller.transferBatches.length === 0
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
            conflictApplyRemaining.checked = false;
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

            AppCheckBox {
                id: conflictApplyRemaining

                visible: center.conflict.batchChild === true
                text: qsTr("Apply Skip or Replace to remaining files in this batch")
                checked: false
                Accessible.name: text
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
