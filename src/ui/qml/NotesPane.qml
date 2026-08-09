pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: pane

    required property var controller
    property string selectedPath: ""
    property string pendingOpenPath: ""
    property string pendingDeletePath: ""
    property string entryDialogMode: "note"
    property string loadedNotePath: ""
    property bool syncingEditor: false
    readonly property bool searching: searchField.text.trim().length > 0
    readonly property var visibleEntries: searching ? controller.noteSearchResults : controller.notes

    function parentPath(path) {
        const separator = path.lastIndexOf("/");
        return separator < 0 ? "" : path.slice(0, separator);
    }

    function selectedFolder() {
        for (let index = 0; index < controller.notes.length; ++index) {
            const entry = controller.notes[index];
            if (entry.path === selectedPath) {
                return entry.folder ? entry.path : parentPath(entry.path);
            }
        }
        return parentPath(controller.activeNotePath);
    }

    function joinedPath(folder, name) {
        return folder.length > 0 ? folder + "/" + name : name;
    }

    function requestOpen(path, focusItem) {
        if (path.length === 0 || path === controller.activeNotePath) {
            return;
        }
        if (controller.activeNoteDirty) {
            pendingOpenPath = path;
            unsavedDialog.focusRestoreItem = focusItem || null;
            unsavedDialog.open();
            return;
        }
        controller.openNote(path);
    }

    function showEntryDialog(mode) {
        entryDialogMode = mode;
        if (mode === "rename") {
            entryPathField.text = selectedPath;
        } else {
            const folder = selectedFolder();
            entryPathField.text = folder.length > 0 ? folder + "/" : "";
        }
        entryDialog.open();
        Qt.callLater(() => {
            entryPathField.forceActiveFocus();
            entryPathField.select(entryPathField.text.lastIndexOf("/") + 1, entryPathField.text.length);
        });
    }

    function commitEntryDialog() {
        let path = entryPathField.text.trim();
        if (entryDialogMode === "note" && path.length > 0 && !path.toLocaleLowerCase().endsWith(".md")) {
            path += ".md";
        }
        let saved = false;
        if (entryDialogMode === "note") {
            saved = controller.createNote(path);
        } else if (entryDialogMode === "folder") {
            saved = controller.createNoteFolder(path);
        } else {
            saved = controller.renameNoteEntry(selectedPath, path);
            if (saved) {
                selectedPath = path;
            }
        }
        if (saved) {
            entryDialog.close();
        }
    }

    function syncEditor() {
        syncingEditor = true;
        loadedNotePath = controller.activeNotePath;
        noteEditor.text = controller.activeNoteContent;
        syncingEditor = false;
        if (loadedNotePath.length > 0) {
            selectedPath = loadedNotePath;
        }
    }

    Component.onCompleted: syncEditor()

    Connections {
        target: pane.controller

        function onNotesChanged() {
            if (pane.controller.activeNotePath !== pane.loadedNotePath || (!pane.controller.activeNoteDirty && pane.controller.activeNoteContent !== noteEditor.text)) {
                pane.syncEditor();
            }
        }
    }

    Timer {
        id: searchDelay

        interval: 140
        repeat: false
        onTriggered: pane.controller.searchNotes(searchField.text)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppIcon {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                name: "file"
                color: Theme.accent
            }

            Text {
                text: qsTr("Notes")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
                font.weight: Font.DemiBold
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("%n item(s)", "", pane.controller.notes.length)
                color: Theme.textSubtle
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }

            ToolButton {
                id: noteActionsButton

                Layout.preferredWidth: 28
                Layout.preferredHeight: 26
                hoverEnabled: true
                onClicked: noteActions.open()
                Accessible.name: qsTr("Note library actions")
                contentItem: AppIcon {
                    name: "more"
                    color: Theme.textSoft
                }
                background: Rectangle {
                    radius: height / 2
                    color: noteActionsButton.down ? Theme.controlPressed : noteActionsButton.hovered ? Theme.controlHover : "transparent"
                }

                AppMenu {
                    id: noteActions

                    y: noteActionsButton.height

                    AppMenuItem {
                        text: qsTr("New note")
                        onTriggered: pane.showEntryDialog("note")
                    }
                    AppMenuItem {
                        text: qsTr("New folder")
                        onTriggered: pane.showEntryDialog("folder")
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Import Markdown note")
                        onTriggered: importDialog.open()
                    }
                    AppMenuItem {
                        text: qsTr("Export active note")
                        enabled: pane.controller.activeNotePath.length > 0
                        onTriggered: exportDialog.open()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Rename or move")
                        enabled: pane.selectedPath.length > 0
                        onTriggered: pane.showEntryDialog("rename")
                    }
                    AppMenuItem {
                        text: qsTr("Delete")
                        enabled: pane.selectedPath.length > 0
                        onTriggered: {
                            pane.pendingDeletePath = pane.selectedPath;
                            deleteDialog.openFrom(noteActionsButton);
                        }
                    }
                }
            }
        }

        AppTextField {
            id: searchField

            Layout.fillWidth: true
            compact: true
            placeholderText: qsTr("Search notes")
            accessibleName: qsTr("Search notes")
            onTextChanged: searchDelay.restart()
        }

        StatusMessage {
            Layout.fillWidth: true
            kind: "error"
            text: pane.controller.noteOperationError
        }

        ListView {
            id: noteList

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(190, Math.max(72, contentHeight))
            clip: true
            spacing: 2
            model: pane.visibleEntries
            keyNavigationEnabled: true
            activeFocusOnTab: true
            visible: pane.visibleEntries.length > 0

            delegate: Rectangle {
                id: noteDelegate

                required property var modelData
                required property int index
                readonly property bool isFolder: !pane.searching && Boolean(modelData.folder)
                readonly property string entryPath: modelData.path || ""

                width: ListView.view.width
                height: pane.searching ? 52 : 34
                radius: Theme.radiusSmall
                color: pane.selectedPath === entryPath ? Theme.selectedBackground : noteHover.hovered ? Theme.controlHover : "transparent"
                border.color: activeFocus ? Theme.focus : "transparent"
                border.width: activeFocus ? 2 : 0
                focus: noteDelegate.ListView.isCurrentItem
                Accessible.role: Accessible.ListItem
                Accessible.name: pane.searching ? modelData.title : modelData.name
                Keys.onReturnPressed: {
                    pane.selectedPath = entryPath;
                    if (!isFolder) {
                        pane.requestOpen(entryPath, noteDelegate);
                    }
                }
                Keys.onEnterPressed: {
                    pane.selectedPath = entryPath;
                    if (!isFolder) {
                        pane.requestOpen(entryPath, noteDelegate);
                    }
                }
                Keys.onDeletePressed: {
                    pane.selectedPath = entryPath;
                    pane.pendingDeletePath = entryPath;
                    deleteDialog.openFrom(noteDelegate);
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: pane.searching ? 7 : 7 + Math.max(0, noteDelegate.entryPath.split("/").length - 1) * 12
                    anchors.rightMargin: 7
                    spacing: 7

                    AppIcon {
                        Layout.preferredWidth: 15
                        Layout.preferredHeight: 15
                        name: noteDelegate.isFolder ? "folder" : "file"
                        color: noteDelegate.isFolder ? Theme.warning : Theme.textSoft
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: pane.searching ? noteDelegate.modelData.title : noteDelegate.modelData.name
                            color: Theme.text
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                            font.weight: pane.selectedPath === noteDelegate.entryPath ? Font.DemiBold : Font.Normal
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: pane.searching
                            text: noteDelegate.modelData.snippet || noteDelegate.entryPath
                            color: Theme.textSubtle
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        pane.selectedPath = noteDelegate.entryPath;
                        noteList.currentIndex = noteDelegate.index;
                    }
                    onDoubleTapped: {
                        if (!noteDelegate.isFolder) {
                            pane.requestOpen(noteDelegate.entryPath, noteDelegate);
                        }
                    }
                }

                HoverHandler {
                    id: noteHover
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        StatePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            visible: pane.visibleEntries.length === 0 && (pane.controller.activeNotePath.length === 0 || pane.searching)
            kind: pane.controller.noteSearchState === "loading" ? "loading" : "empty"
            centered: true
            heading: pane.controller.noteSearchState === "loading" ? qsTr("Searching notes") : pane.searching ? qsTr("No matching notes") : qsTr("No notes")
            description: pane.controller.noteSearchState === "loading" ? qsTr("Searching the bounded local Markdown library outside the interface thread.") : pane.searching ? qsTr("Try a different search term.") : qsTr("Create a Markdown note to keep commands, procedures, and host context nearby.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.controller.activeNotePath.length > 0
            radius: Theme.radiusSmall
            color: Theme.controlBackground
            border.color: noteEditor.activeFocus ? Theme.focus : Theme.border
            border.width: noteEditor.activeFocus ? 2 : 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: pane.controller.activeNotePath
                        color: Theme.text
                        elide: Text.ElideMiddle
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        visible: pane.controller.activeNoteDirty
                        implicitWidth: dirtyLabel.implicitWidth + 12
                        implicitHeight: 20
                        radius: height / 2
                        color: Theme.withAlpha(Theme.warning, Theme.dark ? 0.18 : 0.12)

                        Text {
                            id: dirtyLabel

                            anchors.centerIn: parent
                            text: qsTr("Unsaved")
                            color: Theme.warning
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    ToolButton {
                        id: discardButton

                        implicitWidth: 28
                        implicitHeight: 26
                        enabled: pane.controller.activeNoteDirty
                        hoverEnabled: true
                        onClicked: pane.controller.discardActiveNoteChanges()
                        Accessible.name: qsTr("Discard note changes")
                        contentItem: AppIcon {
                            name: "refresh"
                            color: discardButton.enabled ? Theme.textSoft : Theme.textSubtle
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: discardButton.down ? Theme.controlPressed : discardButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Discard changes")
                        }
                    }

                    ToolButton {
                        id: saveButton

                        implicitWidth: 28
                        implicitHeight: 26
                        enabled: pane.controller.activeNoteDirty
                        hoverEnabled: true
                        onClicked: pane.controller.saveActiveNote()
                        Accessible.name: qsTr("Save note")
                        contentItem: AppIcon {
                            name: "save"
                            color: saveButton.enabled ? Theme.accent : Theme.textSubtle
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: saveButton.down ? Theme.controlPressed : saveButton.hovered ? Theme.controlHover : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Save note (Ctrl+S)")
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: noteEditor

                        width: parent.width
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        persistentSelection: true
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        placeholderText: qsTr("Write Markdown notes here…")
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        background: null
                        onTextChanged: {
                            if (!pane.syncingEditor && pane.controller.activeNotePath.length > 0) {
                                pane.controller.updateActiveNoteContent(text);
                            }
                        }
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier) !== 0) {
                                pane.controller.saveActiveNote();
                                event.accepted = true;
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: entryDialog

        anchors.centerIn: parent
        width: Math.min(380, Math.max(260, parent ? parent.width - 32 : 380))
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 18
        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.elevatedBackground
            border.color: Theme.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: pane.entryDialogMode === "note" ? qsTr("New note") : pane.entryDialogMode === "folder" ? qsTr("New folder") : qsTr("Rename or move")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: pane.entryDialogMode === "folder" ? qsTr("Enter a relative folder path.") : qsTr("Enter a relative path. Markdown notes use the .md extension.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }

            AppTextField {
                id: entryPathField

                Layout.fillWidth: true
                accessibleName: qsTr("Note path")
                onAccepted: pane.commitEntryDialog()
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel note operation")
                    onClicked: entryDialog.close()
                }
                ActionButton {
                    text: pane.entryDialogMode === "rename" ? qsTr("Move") : qsTr("Create")
                    accessibleName: text
                    variant: "primary"
                    enabled: entryPathField.text.trim().length > 0 && (pane.entryDialogMode !== "rename" || entryPathField.text.trim() !== pane.selectedPath)
                    onClicked: pane.commitEntryDialog()
                }
            }
        }
    }

    Dialog {
        id: unsavedDialog

        property Item focusRestoreItem: null

        anchors.centerIn: parent
        width: Math.min(400, Math.max(280, parent ? parent.width - 32 : 400))
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 18
        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.elevatedBackground
            border.color: Theme.warning
        }
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            if (restoreItem && restoreItem.visible) {
                Qt.callLater(() => restoreItem.forceActiveFocus());
            }
        }
        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: qsTr("Save note changes?")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("The active note has changes. Save or discard them before opening another note.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Keep editing note")
                    onClicked: unsavedDialog.close()
                }
                ActionButton {
                    text: qsTr("Discard")
                    accessibleName: qsTr("Discard note changes")
                    variant: "destructive"
                    onClicked: {
                        const path = pane.pendingOpenPath;
                        unsavedDialog.close();
                        pane.controller.openNote(path, true);
                    }
                }
                ActionButton {
                    text: qsTr("Save")
                    accessibleName: qsTr("Save note and continue")
                    variant: "primary"
                    onClicked: {
                        const path = pane.pendingOpenPath;
                        if (pane.controller.saveActiveNote()) {
                            unsavedDialog.close();
                            pane.controller.openNote(path);
                        }
                    }
                }
            }
        }
    }

    ConfirmationDialog {
        id: deleteDialog

        heading: qsTr("Delete note entry?")
        description: qsTr("%1 will be removed from the local notes folder. Non-empty folders are removed with their contents.").arg(pane.pendingDeletePath)
        acceptText: qsTr("Delete")
        destructive: true
        onAccepted: {
            if (pane.controller.deleteNoteEntry(pane.pendingDeletePath)) {
                pane.selectedPath = "";
            }
            pane.pendingDeletePath = "";
        }
        onRejected: pane.pendingDeletePath = ""
    }

    FileDialog {
        id: importDialog

        title: qsTr("Import Markdown note")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Markdown files (*.md *.markdown)"), qsTr("All files (*)")]
        onAccepted: pane.controller.importNote(selectedFile, pane.selectedFolder())
    }

    FileDialog {
        id: exportDialog

        title: qsTr("Export active note")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown files (*.md)"), qsTr("All files (*)")]
        onAccepted: pane.controller.exportActiveNote(selectedFile)
    }
}
