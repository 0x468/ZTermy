pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: browser

    required property var controller
    readonly property var directoryModel: controller.activeSftpDirectoryModel
    property string pendingPath: ""
    property string pendingName: ""
    property bool pendingDirectory: false
    property string pendingDownloadPath: ""
    property var pendingDownloadSize: 0

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("SFTP file browser")

    function formatSize(bytes) {
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

    function beginCreateDirectory() {
        nameDialog.mode = "create";
        nameDialog.heading = qsTr("New folder");
        nameDialogField.text = "";
        nameDialog.open();
        Qt.callLater(nameDialogField.forceActiveFocus);
    }

    function beginRename(path, name) {
        pendingPath = path;
        nameDialog.mode = "rename";
        nameDialog.heading = qsTr("Rename %1").arg(name);
        nameDialogField.text = name;
        nameDialog.open();
        Qt.callLater(() => {
            nameDialogField.forceActiveFocus();
            nameDialogField.selectAll();
        });
    }

    function requestDelete(path, name, directory, sourceItem) {
        pendingPath = path;
        pendingName = name;
        pendingDirectory = directory;
        deleteDialog.openFrom(sourceItem);
    }

    function beginDownload(path, size) {
        pendingDownloadPath = path;
        pendingDownloadSize = size;
        downloadDialog.currentFile = path.split("/").pop();
        downloadDialog.open();
    }

    component BrowserToolButton: ToolButton {
        id: toolButton

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitWidth: 30
        implicitHeight: 30
        background: Rectangle {
            radius: width / 2
            color: toolButton.down ? Theme.controlPressed : toolButton.hovered ? Theme.controlHover : "transparent"
            border.color: toolButton.activeFocus ? Theme.focus : "transparent"
            border.width: toolButton.activeFocus ? 2 : 0
        }
        HoverHandler {
            cursorShape: toolButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 3

            BrowserToolButton {
                enabled: browser.controller.activeSftpHomePath.length > 0 && browser.controller.activeSftpState !== "loading"
                onClicked: browser.controller.navigateSftpHome()
                Accessible.name: qsTr("Home folder")
                contentItem: AppIcon {
                    name: "home"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Home folder")
                }
            }

            BrowserToolButton {
                enabled: browser.controller.activeSftpPath !== "/" && browser.controller.activeSftpState !== "loading"
                onClicked: browser.controller.navigateSftpParent()
                Accessible.name: qsTr("Parent folder")
                contentItem: AppIcon {
                    name: "chevron-up"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Parent folder")
                }
            }

            AppTextField {
                id: pathField

                Layout.fillWidth: true
                compact: true
                text: browser.controller.activeSftpPath
                accessibleName: qsTr("Remote path")
                onAccepted: browser.controller.navigateSftpDirectory(text)
                Connections {
                    target: browser.controller
                    function onSftpChanged() {
                        if (!pathField.activeFocus) {
                            pathField.text = browser.controller.activeSftpPath;
                        }
                    }
                }
            }

            BrowserToolButton {
                enabled: browser.controller.recentSftpPaths.length > 0 && browser.controller.activeSftpState !== "loading"
                onClicked: recentPathsMenu.popup()
                Accessible.name: qsTr("Recent remote paths")
                contentItem: AppIcon {
                    name: "history"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Recent paths")
                }
            }

            BrowserToolButton {
                enabled: browser.controller.activeSftpState !== "loading"
                onClicked: browser.controller.refreshSftpDirectory()
                Accessible.name: qsTr("Refresh folder")
                contentItem: AppIcon {
                    name: "refresh"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Refresh")
                }
            }

            BrowserToolButton {
                enabled: browser.controller.activeSftpState === "ready"
                onClicked: uploadDialog.open()
                Accessible.name: qsTr("Upload files")
                contentItem: AppIcon {
                    name: "upload"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Upload files")
                }
            }

            BrowserToolButton {
                enabled: browser.controller.activeSftpState === "ready"
                onClicked: browser.beginCreateDirectory()
                Accessible.name: qsTr("New folder")
                contentItem: AppIcon {
                    name: "new-folder"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("New folder")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7

            AppTextField {
                id: filterField

                Layout.fillWidth: true
                compact: true
                placeholderText: qsTr("Filter files")
                accessibleName: qsTr("Filter remote files")
                onTextChanged: {
                    if (browser.directoryModel) {
                        browser.directoryModel.filterText = text;
                    }
                }
            }

            AppCheckBox {
                id: hiddenFiles

                text: qsTr("Hidden")
                checked: browser.directoryModel ? browser.directoryModel.showHidden : false
                enabled: browser.directoryModel !== null
                Accessible.name: qsTr("Show hidden files")
                onToggled: {
                    if (browser.directoryModel) {
                        browser.directoryModel.showHidden = checked;
                    }
                }
            }
        }

        StatusMessage {
            Layout.fillWidth: true
            visible: browser.controller.activeSftpState === "ready" && browser.controller.activeSftpError.length > 0
            kind: "error"
            text: browser.controller.activeSftpError
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            visible: browser.controller.activeSftpState === "ready" && fileList.count > 0
            color: Theme.controlBackground
            radius: Theme.radiusSmall

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 34
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Name")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.preferredWidth: 82
                    visible: browser.width >= 430
                    text: qsTr("Modified")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }
                Text {
                    Layout.preferredWidth: 62
                    text: qsTr("Size")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        ListView {
            id: fileList

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: browser.controller.activeSftpState === "ready" && count > 0
            clip: true
            spacing: 1
            model: browser.directoryModel
            activeFocusOnTab: true
            keyNavigationEnabled: true

            delegate: Rectangle {
                id: fileDelegate

                required property string name
                required property string remotePath
                required property string entryType
                required property var size
                required property var modifiedUtcSeconds
                required property bool selected
                required property int index

                readonly property bool isDirectory: entryType === "directory"
                readonly property bool isParent: name === ".."

                width: ListView.view.width
                height: 42
                radius: Theme.radiusSmall
                color: selected || ListView.isCurrentItem ? Theme.selectedBackground : fileHover.hovered ? Theme.controlHover : "transparent"
                border.color: activeFocus ? Theme.focus : "transparent"
                focus: ListView.isCurrentItem
                Accessible.role: Accessible.ListItem
                Accessible.name: isDirectory ? qsTr("Folder %1").arg(name) : qsTr("File %1, %2").arg(name).arg(browser.formatSize(size))

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (isDirectory) {
                            browser.controller.navigateSftpDirectory(remotePath);
                        }
                        event.accepted = true;
                    } else if (event.key === Qt.Key_F2 && !isParent) {
                        browser.beginRename(remotePath, name);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Delete && !isParent) {
                        browser.requestDelete(remotePath, name, isDirectory, fileDelegate);
                        event.accepted = true;
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 4
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        name: fileDelegate.isDirectory ? "folder" : "file"
                        color: fileDelegate.isDirectory ? Theme.accent : Theme.textMuted
                    }

                    Text {
                        Layout.fillWidth: true
                        text: fileDelegate.name
                        color: Theme.text
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                    }

                    Text {
                        Layout.preferredWidth: 82
                        visible: browser.width >= 430
                        text: fileDelegate.modifiedUtcSeconds ? Qt.formatDateTime(new Date(Number(fileDelegate.modifiedUtcSeconds) * 1000), "MM-dd HH:mm") : "—"
                        color: Theme.textSubtle
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.preferredWidth: 62
                        text: fileDelegate.isDirectory ? "—" : browser.formatSize(fileDelegate.size)
                        color: Theme.textSubtle
                        horizontalAlignment: Text.AlignRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Row {
                        visible: !fileDelegate.isParent && (fileHover.hovered || fileDelegate.activeFocus || fileDelegate.ListView.isCurrentItem)
                        spacing: 1

                        BrowserToolButton {
                            visible: !fileDelegate.isDirectory
                            width: visible ? 28 : 0
                            height: 30
                            onClicked: browser.beginDownload(fileDelegate.remotePath, fileDelegate.size)
                            Accessible.name: qsTr("Download %1").arg(fileDelegate.name)
                            contentItem: AppIcon {
                                name: "download"
                                color: Theme.textSoft
                            }
                            AppToolTip {
                                text: qsTr("Download")
                            }
                        }

                        BrowserToolButton {
                            width: 28
                            height: 30
                            onClicked: browser.beginRename(fileDelegate.remotePath, fileDelegate.name)
                            Accessible.name: qsTr("Rename %1").arg(fileDelegate.name)
                            contentItem: AppIcon {
                                name: "edit"
                                color: Theme.textSoft
                            }
                            AppToolTip {
                                text: qsTr("Rename")
                            }
                        }

                        BrowserToolButton {
                            width: 28
                            height: 30
                            onClicked: browser.requestDelete(fileDelegate.remotePath, fileDelegate.name, fileDelegate.isDirectory, parent)
                            Accessible.name: qsTr("Delete %1").arg(fileDelegate.name)
                            contentItem: AppIcon {
                                name: "trash"
                                color: Theme.danger
                            }
                            AppToolTip {
                                text: qsTr("Delete")
                            }
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        fileList.currentIndex = fileDelegate.index;
                        if (browser.directoryModel) {
                            browser.directoryModel.clearSelection();
                            browser.directoryModel.setSelected(fileDelegate.index, true);
                        }
                    }
                    onDoubleTapped: {
                        if (fileDelegate.isDirectory) {
                            browser.controller.navigateSftpDirectory(fileDelegate.remotePath);
                        } else {
                            browser.beginDownload(fileDelegate.remotePath, fileDelegate.size);
                        }
                    }
                }

                HoverHandler {
                    id: fileHover
                }
            }

            DropArea {
                id: uploadDropArea

                anchors.fill: parent
                visible: browser.controller.activeSftpState === "ready"
                keys: ["text/uri-list"]
                z: 20
                onDropped: drop => {
                    if (!drop.hasUrls) {
                        return;
                    }
                    for (const url of drop.urls) {
                        browser.controller.enqueueSftpUpload(url.toString());
                    }
                    drop.acceptProposedAction();
                }

                Rectangle {
                    anchors.fill: parent
                    visible: uploadDropArea.containsDrag
                    radius: Theme.radiusControl
                    color: Theme.selectedBackground
                    border.color: Theme.accent
                    border.width: 2

                    Column {
                        anchors.centerIn: parent
                        spacing: 8

                        AppIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 24
                            height: 24
                            name: "upload"
                            color: Theme.accent
                        }

                        Text {
                            text: qsTr("Drop files to upload")
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: browser.controller.activeSftpState === "connecting" || browser.controller.activeSftpState === "loading"
            kind: "loading"
            centered: true
            heading: browser.controller.activeSftpState === "connecting" ? qsTr("Connecting to SFTP") : qsTr("Loading folder")
            description: qsTr("The terminal remains available while files are loaded.")
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: browser.controller.activeSftpState === "ready" && fileList.count === 0
            kind: filterField.text.length > 0 ? "empty" : "empty"
            centered: true
            heading: filterField.text.length > 0 ? qsTr("No matching files") : qsTr("This folder is empty")
            description: filterField.text.length > 0 ? qsTr("Try another filter or show hidden files.") : qsTr("Create a folder or upload files here.")

            ActionButton {
                visible: filterField.text.length === 0
                text: qsTr("New folder")
                onClicked: browser.beginCreateDirectory()
            }
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: browser.controller.activeSftpState === "error"
            kind: "error"
            centered: true
            heading: qsTr("SFTP is unavailable")
            description: browser.controller.activeSftpError

            ActionButton {
                text: qsTr("Try again")
                onClicked: browser.controller.refreshSftpDirectory()
            }
        }
    }

    AppMenu {
        id: recentPathsMenu

        Repeater {
            model: browser.controller.recentSftpPaths

            AppMenuItem {
                required property string modelData

                text: modelData
                Accessible.name: qsTr("Open recent remote path %1").arg(modelData)
                onTriggered: browser.controller.navigateSftpDirectory(modelData)
            }
        }
    }

    Popup {
        id: nameDialog

        property string mode: "create"
        property string heading: ""

        anchors.centerIn: Overlay.overlay
        width: Math.min(340, browser.width - 24)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 14
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.raisedBackground
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: nameDialog.heading
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody + 2
                font.weight: Font.DemiBold
            }

            AppTextField {
                id: nameDialogField

                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                accessibleName: nameDialog.heading
                onAccepted: acceptNameButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")
                    onClicked: nameDialog.close()
                }

                ActionButton {
                    id: acceptNameButton

                    text: nameDialog.mode === "create" ? qsTr("Create") : qsTr("Rename")
                    variant: "primary"
                    enabled: nameDialogField.text.trim().length > 0
                    onClicked: {
                        const accepted = nameDialog.mode === "create" ? browser.controller.createSftpDirectory(nameDialogField.text) : browser.controller.renameSftpEntry(browser.pendingPath, nameDialogField.text);
                        if (accepted) {
                            nameDialog.close();
                        }
                    }
                }
            }
        }
    }

    ConfirmationDialog {
        id: deleteDialog

        heading: browser.pendingDirectory ? qsTr("Delete folder?") : qsTr("Delete file?")
        description: qsTr("%1 will be permanently removed from the remote host.").arg(browser.pendingName)
        acceptText: qsTr("Delete")
        rejectText: qsTr("Cancel")
        destructive: true
        onAccepted: {
            browser.controller.removeSftpEntry(browser.pendingPath, browser.pendingDirectory);
            close();
        }
        onClosed: {
            browser.pendingPath = "";
            browser.pendingName = "";
            browser.pendingDirectory = false;
        }
    }

    FileDialog {
        id: uploadDialog

        title: qsTr("Upload files")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const file of selectedFiles) {
                browser.controller.enqueueSftpUpload(file.toString());
            }
        }
    }

    FileDialog {
        id: downloadDialog

        title: qsTr("Save remote file")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            browser.controller.enqueueSftpDownload(browser.pendingDownloadPath, selectedFile.toString(), browser.pendingDownloadSize);
            browser.pendingDownloadPath = "";
            browser.pendingDownloadSize = 0;
        }
        onRejected: {
            browser.pendingDownloadPath = "";
            browser.pendingDownloadSize = 0;
        }
    }
}
