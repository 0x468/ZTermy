pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: browser

    objectName: "sftpBrowser"

    required property var controller
    readonly property var directoryModel: controller.activeSftpDirectoryModel
    readonly property bool compactToolbar: width < 520
    property string pendingPath: ""
    property string pendingName: ""
    property bool pendingDirectory: false
    property string pendingDownloadPath: ""
    property var pendingDownloadSize: 0
    property var pendingDownloadModified: -1

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
        nameDialog.mode = "createDirectory";
        nameDialog.heading = qsTr("New folder");
        nameDialogField.text = "";
        nameDialog.open();
        Qt.callLater(nameDialogField.forceActiveFocus);
    }

    function beginCreateFile() {
        nameDialog.mode = "createFile";
        nameDialog.heading = qsTr("New file");
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
        if (!controller.sftpConfirmDelete) {
            controller.removeSftpEntry(path, directory);
            return;
        }
        pendingPath = path;
        pendingName = name;
        pendingDirectory = directory;
        deleteDialog.openFrom(sourceItem);
    }

    function beginDownload(path, size, modified) {
        pendingDownloadPath = path;
        pendingDownloadSize = size;
        pendingDownloadModified = modified === undefined || modified === null ? -1 : modified;
        downloadDialog.currentFile = path.split("/").pop();
        downloadDialog.open();
    }

    function sortBy(column) {
        const ascending = controller.activeSftpSortColumn === column ? !controller.activeSftpSortAscending : true;
        controller.setSftpSort(column, ascending);
    }

    function sortLabel(label, column) {
        return controller.activeSftpSortColumn === column ? label + (controller.activeSftpSortAscending ? "  ↑" : "  ↓") : label;
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
                objectName: "sftpHomeButton"
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
                objectName: "sftpParentButton"
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

                objectName: "sftpPathField"

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
                objectName: "sftpBookmarkButton"
                enabled: browser.controller.activeSftpState === "ready" && browser.controller.activeSftpPath.length > 0
                onClicked: {
                    if (browser.controller.bookmarkedSftpPaths.length === 0 && !browser.controller.activeSftpPathBookmarked) {
                        browser.controller.toggleActiveSftpBookmark();
                    } else {
                        bookmarksMenu.popup();
                    }
                }
                Accessible.name: browser.controller.activeSftpPathBookmarked ? qsTr("Manage bookmark for current path") : qsTr("Bookmark current path")
                contentItem: AppIcon {
                    name: "bookmark"
                    color: browser.controller.activeSftpPathBookmarked ? Theme.accent : parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: parent.Accessible.name
                }
            }

            BrowserToolButton {
                objectName: "sftpCopyPathButton"
                enabled: browser.controller.activeSftpPath.length > 0
                onClicked: browser.controller.copyActiveSftpPath()
                Accessible.name: qsTr("Copy remote path")
                contentItem: AppIcon {
                    name: "copy"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("Copy path")
                }
            }

            BrowserToolButton {
                objectName: "sftpRecentPathsButton"
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
                objectName: "sftpLocateTerminalDirectoryButton"
                visible: !browser.compactToolbar
                enabled: browser.controller.activeTerminalWorkingDirectory.length > 0 && browser.controller.activeSftpState !== "loading"
                onClicked: browser.controller.navigateSftpToTerminalDirectory()
                Accessible.name: qsTr("Open terminal working directory")
                contentItem: AppIcon {
                    name: "locate"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: parent.enabled ? qsTr("Open terminal directory: %1").arg(browser.controller.activeTerminalWorkingDirectory) : qsTr("The shell has not reported its working directory")
                }
            }

            BrowserToolButton {
                objectName: "sftpFollowTerminalDirectoryButton"
                visible: !browser.compactToolbar
                enabled: browser.controller.activeTerminalWorkingDirectory.length > 0
                onClicked: browser.controller.setSftpFollowTerminalDirectory(!browser.controller.activeSftpFollowTerminalDirectory)
                Accessible.name: browser.controller.activeSftpFollowTerminalDirectory ? qsTr("Stop following terminal directory") : qsTr("Follow terminal directory")
                Accessible.checked: browser.controller.activeSftpFollowTerminalDirectory
                contentItem: AppIcon {
                    name: "follow"
                    color: browser.controller.activeSftpFollowTerminalDirectory ? Theme.accent : parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: parent.Accessible.name
                }
            }

            BrowserToolButton {
                objectName: "sftpViewModeButton"
                visible: !browser.compactToolbar
                enabled: browser.directoryModel !== null
                onClicked: browser.controller.setSftpViewMode(browser.controller.activeSftpViewMode === "tree" ? "list" : "tree")
                Accessible.name: browser.controller.activeSftpViewMode === "tree" ? qsTr("Switch to list view") : qsTr("Switch to tree view")
                contentItem: AppIcon {
                    name: browser.controller.activeSftpViewMode === "tree" ? "list" : "tree"
                    color: browser.controller.activeSftpViewMode === "tree" ? Theme.accent : Theme.textSoft
                }
                AppToolTip {
                    text: parent.Accessible.name
                }
            }

            BrowserToolButton {
                objectName: "sftpRefreshButton"
                visible: !browser.compactToolbar
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
                objectName: "sftpUploadButton"
                visible: !browser.compactToolbar
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
                objectName: "sftpNewFolderButton"
                visible: !browser.compactToolbar
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

            BrowserToolButton {
                objectName: "sftpNewFileButton"
                visible: !browser.compactToolbar
                enabled: browser.controller.activeSftpState === "ready"
                onClicked: browser.beginCreateFile()
                Accessible.name: qsTr("New file")
                contentItem: AppIcon {
                    name: "new-file"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("New file")
                }
            }

            BrowserToolButton {
                objectName: "sftpMoreActionsButton"
                visible: true
                enabled: browser.controller.activeSftpState !== "loading"
                onClicked: browserActionsMenu.popup()
                Accessible.name: qsTr("More file browser actions")
                contentItem: AppIcon {
                    name: "more"
                    color: parent.enabled ? Theme.textSoft : Theme.textSubtle
                }
                AppToolTip {
                    text: qsTr("More actions")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7

            AppTextField {
                id: filterField

                objectName: "sftpFilterField"

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

                objectName: "sftpHiddenFilesCheckBox"

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

                ToolButton {
                    id: nameHeader

                    objectName: "sftpNameHeader"

                    Layout.fillWidth: true
                    text: browser.sortLabel(qsTr("Name"), "name")
                    onClicked: browser.sortBy("name")
                    contentItem: Text {
                        text: nameHeader.text
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
                ToolButton {
                    id: modifiedHeader

                    objectName: "sftpModifiedHeader"

                    Layout.preferredWidth: 82
                    visible: !browser.compactToolbar && browser.controller.activeSftpShowModifiedColumn
                    text: browser.sortLabel(qsTr("Modified"), "modified")
                    onClicked: browser.sortBy("modified")
                    contentItem: Text {
                        text: modifiedHeader.text
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
                ToolButton {
                    id: sizeHeader

                    objectName: "sftpSizeHeader"

                    Layout.preferredWidth: 62
                    visible: browser.controller.activeSftpShowSizeColumn
                    text: browser.sortLabel(qsTr("Size"), "size")
                    onClicked: browser.sortBy("size")
                    contentItem: Text {
                        text: sizeHeader.text
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
                ToolButton {
                    id: typeHeader

                    objectName: "sftpTypeHeader"

                    Layout.preferredWidth: 64
                    visible: !browser.compactToolbar && browser.controller.activeSftpShowTypeColumn
                    text: browser.sortLabel(qsTr("Type"), "type")
                    onClicked: browser.sortBy("type")
                    contentItem: Text {
                        text: typeHeader.text
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
            }
        }

        ListView {
            id: fileList

            objectName: "sftpFileList"

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
                required property int depth
                required property bool expanded
                required property bool expandable
                required property bool loading
                required property string loadError
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
                Accessible.description: loadError

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (browser.controller.activeSftpViewMode === "tree" && expandable) {
                            browser.directoryModel.toggleExpanded(index);
                        } else if (isDirectory) {
                            browser.controller.navigateSftpDirectory(remotePath);
                        }
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Right && expandable && browser.controller.activeSftpViewMode === "tree") {
                        if (!expanded) {
                            browser.directoryModel.toggleExpanded(index);
                        } else if (index + 1 < fileList.count) {
                            fileList.currentIndex = index + 1;
                        }
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Left && expandable && expanded && browser.controller.activeSftpViewMode === "tree") {
                        browser.directoryModel.toggleExpanded(index);
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
                    anchors.leftMargin: 9 + (browser.controller.activeSftpViewMode === "tree" ? fileDelegate.depth * 18 : 0)
                    anchors.rightMargin: 4
                    spacing: 8

                    BrowserToolButton {
                        visible: browser.controller.activeSftpViewMode === "tree" && fileDelegate.expandable
                        Layout.preferredWidth: visible ? 22 : 0
                        Layout.preferredHeight: 28
                        onClicked: browser.directoryModel.toggleExpanded(fileDelegate.index)
                        Accessible.name: fileDelegate.expanded ? qsTr("Collapse %1").arg(fileDelegate.name) : qsTr("Expand %1").arg(fileDelegate.name)
                        contentItem: AppIcon {
                            name: fileDelegate.expanded ? "chevron-down" : "chevron-right"
                            color: fileDelegate.loadError.length > 0 ? Theme.danger : Theme.textSoft
                        }
                        AppToolTip {
                            text: fileDelegate.loadError.length > 0 ? fileDelegate.loadError : parent.Accessible.name
                        }
                    }

                    Item {
                        visible: browser.controller.activeSftpViewMode === "tree" && !fileDelegate.expandable
                        Layout.preferredWidth: visible ? 22 : 0
                    }

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
                        visible: !browser.compactToolbar && browser.controller.activeSftpShowModifiedColumn
                        text: fileDelegate.modifiedUtcSeconds ? Qt.formatDateTime(new Date(Number(fileDelegate.modifiedUtcSeconds) * 1000), "MM-dd HH:mm") : "—"
                        color: Theme.textSubtle
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.preferredWidth: 62
                        visible: browser.controller.activeSftpShowSizeColumn
                        text: fileDelegate.isDirectory ? "—" : browser.formatSize(fileDelegate.size)
                        color: Theme.textSubtle
                        horizontalAlignment: Text.AlignRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.preferredWidth: 64
                        visible: !browser.compactToolbar && browser.controller.activeSftpShowTypeColumn
                        text: fileDelegate.isDirectory ? qsTr("Folder") : fileDelegate.entryType === "file" ? qsTr("File") : fileDelegate.entryType
                        color: Theme.textSubtle
                        elide: Text.ElideRight
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
                            onClicked: browser.beginDownload(fileDelegate.remotePath, fileDelegate.size, fileDelegate.modifiedUtcSeconds)
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
                            browser.beginDownload(fileDelegate.remotePath, fileDelegate.size, fileDelegate.modifiedUtcSeconds);
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

    AppMenu {
        id: bookmarksMenu

        AppMenuItem {
            text: browser.controller.activeSftpPathBookmarked ? qsTr("Remove bookmark") : qsTr("Bookmark this path")
            onTriggered: browser.controller.toggleActiveSftpBookmark()
        }

        AppMenuSeparator {
            visible: browser.controller.bookmarkedSftpPaths.length > 0
        }

        Repeater {
            model: browser.controller.bookmarkedSftpPaths

            AppMenuItem {
                required property string modelData

                text: modelData
                Accessible.name: qsTr("Open bookmarked remote path %1").arg(modelData)
                onTriggered: browser.controller.navigateSftpDirectory(modelData)
            }
        }
    }

    AppMenu {
        id: browserActionsMenu

        objectName: "sftpBrowserActionsMenu"

        AppMenuItem {
            text: qsTr("Refresh")
            onTriggered: browser.controller.refreshSftpDirectory()
        }
        AppMenuItem {
            text: qsTr("Open terminal working directory")
            enabled: browser.controller.activeTerminalWorkingDirectory.length > 0
            onTriggered: browser.controller.navigateSftpToTerminalDirectory()
        }
        AppMenuItem {
            text: browser.controller.activeSftpFollowTerminalDirectory ? qsTr("Stop following terminal directory") : qsTr("Follow terminal directory")
            enabled: browser.controller.activeTerminalWorkingDirectory.length > 0
            onTriggered: browser.controller.setSftpFollowTerminalDirectory(!browser.controller.activeSftpFollowTerminalDirectory)
        }
        AppMenuItem {
            text: browser.controller.activeSftpViewMode === "tree" ? qsTr("List view") : qsTr("Tree view")
            onTriggered: browser.controller.setSftpViewMode(browser.controller.activeSftpViewMode === "tree" ? "list" : "tree")
        }
        AppMenuItem {
            text: qsTr("Upload files")
            enabled: browser.controller.activeSftpState === "ready"
            onTriggered: uploadDialog.open()
        }
        AppMenuItem {
            text: qsTr("New folder")
            enabled: browser.controller.activeSftpState === "ready"
            onTriggered: browser.beginCreateDirectory()
        }
        AppMenuItem {
            text: qsTr("New file")
            enabled: browser.controller.activeSftpState === "ready"
            onTriggered: browser.beginCreateFile()
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "sftpDirectoriesFirstAction"
            text: qsTr("Directories first")
            checkable: true
            checked: browser.controller.activeSftpDirectoriesFirst
            onTriggered: browser.controller.setSftpDirectoriesFirst(checked)
        }
        AppMenuItem {
            objectName: "sftpModifiedColumnAction"
            text: qsTr("Modified column")
            checkable: true
            checked: browser.controller.activeSftpShowModifiedColumn
            onTriggered: browser.controller.setSftpVisibleColumns(checked, browser.controller.activeSftpShowSizeColumn, browser.controller.activeSftpShowTypeColumn)
        }
        AppMenuItem {
            objectName: "sftpSizeColumnAction"
            text: qsTr("Size column")
            checkable: true
            checked: browser.controller.activeSftpShowSizeColumn
            onTriggered: browser.controller.setSftpVisibleColumns(browser.controller.activeSftpShowModifiedColumn, checked, browser.controller.activeSftpShowTypeColumn)
        }
        AppMenuItem {
            objectName: "sftpTypeColumnAction"
            text: qsTr("Type column")
            checkable: true
            checked: browser.controller.activeSftpShowTypeColumn
            onTriggered: browser.controller.setSftpVisibleColumns(browser.controller.activeSftpShowModifiedColumn, browser.controller.activeSftpShowSizeColumn, checked)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "sftpUtf8EncodingAction"
            text: qsTr("Filename encoding: UTF-8")
            checkable: true
            checked: browser.controller.activeSftpFilenameEncoding === "utf-8"
            onTriggered: browser.controller.setSftpFilenameEncoding("utf-8")
        }
        AppMenuItem {
            objectName: "sftpGb18030EncodingAction"
            text: qsTr("Filename encoding: GB18030")
            checkable: true
            checked: browser.controller.activeSftpFilenameEncoding === "gb18030"
            onTriggered: browser.controller.setSftpFilenameEncoding("gb18030")
        }
    }

    Popup {
        id: nameDialog

        property string mode: "createDirectory"
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

                    text: nameDialog.mode === "rename" ? qsTr("Rename") : qsTr("Create")
                    variant: "primary"
                    enabled: nameDialogField.text.trim().length > 0
                    onClicked: {
                        const accepted = nameDialog.mode === "createDirectory" ? browser.controller.createSftpDirectory(nameDialogField.text) : nameDialog.mode === "createFile" ? browser.controller.createSftpFile(nameDialogField.text) : browser.controller.renameSftpEntry(browser.pendingPath, nameDialogField.text);
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
            browser.controller.enqueueSftpDownload(browser.pendingDownloadPath, selectedFile.toString(), browser.pendingDownloadSize, browser.pendingDownloadModified);
            browser.pendingDownloadPath = "";
            browser.pendingDownloadSize = 0;
            browser.pendingDownloadModified = -1;
        }
        onRejected: {
            browser.pendingDownloadPath = "";
            browser.pendingDownloadSize = 0;
            browser.pendingDownloadModified = -1;
        }
    }
}
