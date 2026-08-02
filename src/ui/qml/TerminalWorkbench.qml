pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: workbench

    required property var controller
    required property var activeTab
    required property string panelSide
    property real panelWidth: 360
    property real dragStartGlobalX: 0
    property real dragStartWidth: panelWidth
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string historyScope: "profile"
    property string appliedHistorySearch: ""
    readonly property string currentPage: activeTab ? activeTab.workbenchPage : "history"
    readonly property var historySource: historyScope === "global" ? controller.terminalGlobalHistory : controller.terminalHistory
    readonly property var filteredQuickCommands: {
        const needle = quickCommandSearch.text.trim().toLocaleLowerCase();
        if (needle.length === 0) {
            return controller.quickCommands;
        }
        return controller.quickCommands.filter(command => command.name.toLocaleLowerCase().includes(needle) || command.command.toLocaleLowerCase().includes(needle) || command.description.toLocaleLowerCase().includes(needle));
    }
    readonly property var filteredHistory: {
        const needle = appliedHistorySearch.trim().toLocaleLowerCase();
        if (needle.length === 0) {
            return historySource;
        }
        return historySource.filter(entry => entry.command.toLocaleLowerCase().includes(needle) || (entry.sourceLabel || "").toLocaleLowerCase().includes(needle));
    }

    signal panelWidthRequested(real width)
    signal insertRequested(string command)
    signal runRequested(string command, var sourceItem)
    signal closeRequested

    component WorkbenchToolButton: ToolButton {
        id: control

        property bool selected: false

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Keys.onReturnPressed: event => {
            if (!event.isAutoRepeat) {
                control.click();
            }
            event.accepted = true;
        }
        Keys.onEnterPressed: event => {
            if (!event.isAutoRepeat) {
                control.click();
            }
            event.accepted = true;
        }

        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height)
                height: width
                radius: width / 2
                color: control.down ? Theme.controlPressed : control.hovered ? Theme.controlHover : "transparent"
                border.color: control.activeFocus ? Theme.focus : "transparent"
                border.width: control.activeFocus ? 2 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: control.selected ? 12 : 0
                height: 2
                radius: 1
                color: Theme.accent

                Behavior on width {
                    NumberAnimation {
                        duration: Theme.motionFast
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        HoverHandler {
            cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    component ScopeButton: Button {
        id: scopeControl

        property bool active: false

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: 24
        leftPadding: 8
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        contentItem: Text {
            text: scopeControl.text
            color: scopeControl.active ? Theme.text : Theme.textMuted
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
            font.weight: scopeControl.active ? Font.DemiBold : Font.Normal
        }
        background: Rectangle {
            radius: height / 2
            color: scopeControl.active ? Theme.selectedBackground : scopeControl.hovered ? Theme.controlHover : "transparent"
            border.color: scopeControl.activeFocus ? Theme.focus : "transparent"
            border.width: scopeControl.activeFocus ? 2 : 0
        }

        HoverHandler {
            cursorShape: scopeControl.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    color: Theme.panelBackground
    border.color: Theme.border
    focus: visible
    Keys.onEscapePressed: closeRequested()
    Accessible.role: Accessible.Pane
    Accessible.name: currentPage === "sftp" ? qsTr("SFTP file browser") : currentPage === "history" ? qsTr("Command history") : qsTr("Scripts")
    onVisibleChanged: {
        if (visible && currentPage === "history" && controller.terminalHistoryState === "idle") {
            controller.refreshTerminalHistory();
        }
    }

    function beginNewCommand(prefill) {
        const command = prefill || "";
        commandEditor.editingId = "";
        commandName.text = command.length > 0 ? command.split("\n")[0].slice(0, 64) : "";
        commandText.text = command;
        commandDescription.text = "";
        commandShell.currentIndex = 0;
        commandEditor.visible = true;
        Qt.callLater(commandName.forceActiveFocus);
    }

    function saveHistoryCommand(command) {
        controller.toggleTerminalWorkbench("scripts");
        Qt.callLater(() => beginNewCommand(command));
    }

    function beginEditCommand(command) {
        commandEditor.editingId = command.id;
        commandName.text = command.name;
        commandText.text = command.command;
        commandDescription.text = command.description;
        commandShell.currentIndex = command.shell === "posix" ? 1 : command.shell === "powershell" ? 2 : 0;
        commandEditor.visible = true;
        Qt.callLater(commandName.forceActiveFocus);
    }

    function requestDeleteCommand(command, focusItem) {
        pendingDeleteId = command.id;
        pendingDeleteName = command.name;
        deleteCommandDialog.openFrom(focusItem);
    }

    function quickCommandIndex(id) {
        for (let index = 0; index < controller.quickCommands.length; ++index) {
            if (controller.quickCommands[index].id === id) {
                return index;
            }
        }
        return -1;
    }

    function moveQuickCommand(id, offset) {
        const index = quickCommandIndex(id);
        if (index >= 0) {
            controller.moveQuickCommand(id, index + offset);
        }
    }

    MouseArea {
        id: resizeHandle

        objectName: "terminalWorkbenchResizeHandle"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 7
        x: workbench.panelSide === "right" ? 0 : parent.width - width
        z: 20
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: mouse => {
            workbench.dragStartGlobalX = mapToGlobal(mouse.x, mouse.y).x;
            workbench.dragStartWidth = workbench.panelWidth;
        }
        onPositionChanged: mouse => {
            if (!pressed) {
                return;
            }
            const globalX = mapToGlobal(mouse.x, mouse.y).x;
            const delta = workbench.panelSide === "left" ? globalX - workbench.dragStartGlobalX : workbench.dragStartGlobalX - globalX;
            workbench.panelWidthRequested(Math.max(320, Math.min(800, workbench.dragStartWidth + delta)));
        }
    }

    Timer {
        id: historySearchDelay

        interval: 120
        repeat: false
        onTriggered: workbench.appliedHistorySearch = historySearch.text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.chromeBackground

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 7
                spacing: 4

                WorkbenchToolButton {
                    id: sftpPageButton

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    checkable: true
                    checked: workbench.currentPage === "sftp"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("sftp")
                    Accessible.name: qsTr("SFTP file browser")
                    contentItem: AppIcon {
                        name: "folder"
                        color: sftpPageButton.checked ? Theme.accent : Theme.textSoft
                    }

                    AppToolTip {
                        text: qsTr("SFTP files")
                    }
                }

                WorkbenchToolButton {
                    id: historyPageButton

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    checkable: true
                    checked: workbench.currentPage === "history"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("history")
                    Accessible.name: qsTr("Command history")
                    contentItem: AppIcon {
                        name: "history"
                        color: historyPageButton.checked ? Theme.accent : Theme.textSoft
                    }
                }

                WorkbenchToolButton {
                    id: quickCommandsPageButton

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    checkable: true
                    checked: workbench.currentPage === "scripts"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("scripts")
                    Accessible.name: qsTr("Scripts")
                    contentItem: AppIcon {
                        name: "commands"
                        color: quickCommandsPageButton.checked ? Theme.accent : Theme.textSoft
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                WorkbenchToolButton {
                    objectName: "moveTerminalWorkbenchButton"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    onClicked: workbench.controller.moveTerminalWorkbench()
                    Accessible.name: workbench.panelSide === "left" ? qsTr("Move terminal workbench right") : qsTr("Move terminal workbench left")
                    contentItem: AppIcon {
                        name: "swap-horizontal"
                        color: Theme.textSoft
                    }
                }

                WorkbenchToolButton {
                    objectName: "closeTerminalWorkbenchButton"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    onClicked: workbench.closeRequested()
                    Accessible.name: qsTr("Close terminal workbench")
                    contentItem: AppIcon {
                        name: "close"
                        color: Theme.textSoft
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                SftpBrowser {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: workbench.currentPage === "sftp"
                    controller: workbench.controller
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: workbench.currentPage === "history"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppTextField {
                                id: historySearch

                                Layout.fillWidth: true
                                compact: true
                                placeholderText: qsTr("Search command history")
                                accessibleName: qsTr("Search command history")
                                onTextChanged: historySearchDelay.restart()
                            }

                            WorkbenchToolButton {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                visible: workbench.historyScope === "profile"
                                enabled: workbench.controller.terminalHistoryState !== "loading"
                                onClicked: workbench.controller.refreshTerminalHistory()
                                Accessible.name: qsTr("Refresh command history")
                                contentItem: AppIcon {
                                    name: "history"
                                    color: Theme.text
                                    rotation: parent.enabled ? 0 : 180

                                    Behavior on rotation {
                                        NumberAnimation {
                                            duration: Theme.motionMedium
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            spacing: 3

                            ScopeButton {
                                Layout.maximumWidth: 160
                                active: workbench.historyScope === "profile"
                                text: workbench.activeTab && workbench.activeTab.title.length > 0 ? workbench.activeTab.title : qsTr("Current profile")
                                Accessible.name: qsTr("Current profile history")
                                onClicked: workbench.historyScope = "profile"
                            }

                            ScopeButton {
                                active: workbench.historyScope === "global"
                                text: qsTr("Global")
                                Accessible.name: qsTr("Global command history")
                                onClicked: workbench.historyScope = "global"
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("%n command(s)", "", workbench.historySource.length)
                                color: Theme.textSubtle
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        StatusMessage {
                            Layout.fillWidth: true
                            kind: "error"
                            text: workbench.historyScope === "profile" ? workbench.controller.terminalHistoryError : ""
                        }

                        ListView {
                            id: historyList

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: workbench.filteredHistory.length > 0
                            clip: true
                            spacing: 1
                            model: workbench.filteredHistory
                            keyNavigationEnabled: true
                            activeFocusOnTab: true

                            delegate: Rectangle {
                                id: historyDelegate

                                required property var modelData
                                required property int index

                                width: ListView.view.width
                                height: workbench.historyScope === "global" && (modelData.sourceLabel || "").length > 0 ? 48 : 38
                                radius: Theme.radiusSmall
                                color: historyDelegate.ListView.isCurrentItem ? Theme.selectedBackground : historyHover.hovered ? Theme.controlHover : "transparent"
                                border.color: historyDelegate.activeFocus ? Theme.focus : "transparent"
                                focus: historyDelegate.ListView.isCurrentItem
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.command
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                        if ((event.modifiers & Qt.ControlModifier) !== 0) {
                                            workbench.insertRequested(historyDelegate.modelData.command);
                                        } else {
                                            workbench.runRequested(historyDelegate.modelData.command, historyDelegate);
                                        }
                                        event.accepted = true;
                                    } else if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier) !== 0) {
                                        workbench.saveHistoryCommand(historyDelegate.modelData.command);
                                        event.accepted = true;
                                    }
                                }

                                Text {
                                    id: historyCommand

                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 92
                                    anchors.top: parent.top
                                    anchors.topMargin: 6
                                    text: historyDelegate.modelData.command
                                    color: Theme.text
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 4
                                    visible: workbench.historyScope === "global" && (historyDelegate.modelData.sourceLabel || "").length > 0
                                    text: historyDelegate.modelData.sourceLabel || ""
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: 9
                                }

                                Row {
                                    id: historyActions

                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: historyHover.hovered || historyDelegate.ListView.isCurrentItem || historyDelegate.activeFocus
                                    spacing: 2

                                    WorkbenchToolButton {
                                        id: runHistoryCommandButton

                                        width: 26
                                        height: 26
                                        onClicked: workbench.runRequested(historyDelegate.modelData.command, runHistoryCommandButton)
                                        Accessible.name: qsTr("Run history command")
                                        contentItem: AppIcon {
                                            name: "play"
                                            color: Theme.textSoft
                                        }

                                        AppToolTip {
                                            text: qsTr("Run")
                                        }
                                    }

                                    WorkbenchToolButton {
                                        width: 26
                                        height: 26
                                        onClicked: workbench.insertRequested(historyDelegate.modelData.command)
                                        Accessible.name: qsTr("Insert history command")
                                        contentItem: AppIcon {
                                            name: "compose"
                                            color: Theme.textSoft
                                        }

                                        AppToolTip {
                                            text: qsTr("Insert")
                                        }
                                    }

                                    WorkbenchToolButton {
                                        width: 26
                                        height: 26
                                        onClicked: workbench.saveHistoryCommand(historyDelegate.modelData.command)
                                        Accessible.name: qsTr("Save history command")
                                        contentItem: AppIcon {
                                            name: "save"
                                            color: Theme.textSoft
                                        }

                                        AppToolTip {
                                            text: qsTr("Save as code snippet")
                                        }
                                    }
                                }

                                TapHandler {
                                    onTapped: historyList.currentIndex = historyDelegate.index
                                }

                                HoverHandler {
                                    id: historyHover
                                }
                            }

                            ScrollBar.vertical: ScrollBar {}
                        }

                        StatePanel {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: workbench.filteredHistory.length === 0
                            kind: workbench.historyScope === "profile" && workbench.controller.terminalHistoryState === "loading" ? "loading" : "empty"
                            centered: true
                            heading: workbench.historyScope === "profile" && workbench.controller.terminalHistoryState === "loading" ? qsTr("Loading history") : historySearch.text.length > 0 ? qsTr("No matching history") : workbench.historyScope === "global" ? qsTr("No global command history") : qsTr("No command history")
                            description: workbench.historyScope === "profile" && workbench.controller.terminalHistoryState === "loading" ? qsTr("Reading a bounded snapshot outside the interface thread.") : historySearch.text.length > 0 ? qsTr("Try a different search term.") : workbench.historyScope === "global" ? qsTr("Commands from open terminal sessions appear here without being written to disk.") : qsTr("History is read from the active shell and remains in memory for this tab.")

                            ActionButton {
                                text: qsTr("Refresh history")
                                visible: workbench.historyScope === "profile" && workbench.controller.terminalHistoryState !== "loading"
                                accessibleName: qsTr("Refresh command history")
                                onClicked: workbench.controller.refreshTerminalHistory()
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 8
                    spacing: 6
                    visible: workbench.currentPage === "scripts"

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            name: "commands"
                            color: Theme.accent
                        }

                        Text {
                            text: qsTr("Code snippets")
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                            font.weight: Font.DemiBold
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            text: qsTr("%n item(s)", "", workbench.controller.quickCommands.length)
                            color: Theme.textSubtle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppTextField {
                            id: quickCommandSearch

                            Layout.fillWidth: true
                            compact: true
                            placeholderText: qsTr("Search scripts and code snippets")
                            accessibleName: qsTr("Search scripts and code snippets")
                        }

                        WorkbenchToolButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            onClicked: workbench.beginNewCommand("")
                            Accessible.name: qsTr("New code snippet")
                            contentItem: AppIcon {
                                name: "plus"
                                color: Theme.text
                            }
                        }
                    }

                    Rectangle {
                        id: commandEditor

                        property string editingId: ""

                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 246 : 0
                        visible: false
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 7

                            AppTextField {
                                id: commandName

                                Layout.fillWidth: true
                                compact: true
                                placeholderText: qsTr("Snippet name")
                                accessibleName: qsTr("Code snippet name")
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 74
                                clip: true

                                TextArea {
                                    id: commandText

                                    placeholderText: qsTr("Command or multiline shell text")
                                    color: Theme.text
                                    placeholderTextColor: Theme.textMuted
                                    selectionColor: Theme.accent
                                    selectedTextColor: Theme.accentText
                                    wrapMode: TextEdit.NoWrap
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textBody
                                    Accessible.name: qsTr("Code snippet command")
                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        color: Theme.fieldBackground
                                        border.color: commandText.activeFocus ? Theme.focus : Theme.border
                                    }
                                }
                            }

                            AppTextField {
                                id: commandDescription

                                Layout.fillWidth: true
                                compact: true
                                placeholderText: qsTr("Description (optional)")
                                accessibleName: qsTr("Code snippet description")
                            }

                            AppComboBox {
                                id: commandShell

                                Layout.fillWidth: true
                                model: ["any", "posix", "powershell"]
                                displayTextModel: [qsTr("Any shell"), qsTr("POSIX shell"), qsTr("PowerShell")]
                                accessibleName: qsTr("Code snippet shell scope")
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Item {
                                    Layout.fillWidth: true
                                }

                                ActionButton {
                                    text: qsTr("Cancel")
                                    onClicked: commandEditor.visible = false
                                }

                                ActionButton {
                                    text: qsTr("Save")
                                    variant: "primary"
                                    enabled: commandName.text.trim().length > 0 && commandText.text.trim().length > 0
                                    onClicked: {
                                        const shell = commandShell.currentIndex === 1 ? "posix" : commandShell.currentIndex === 2 ? "powershell" : "any";
                                        if (workbench.controller.saveQuickCommand(commandEditor.editingId, commandName.text, commandText.text, commandDescription.text, shell)) {
                                            commandEditor.visible = false;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    StatusMessage {
                        Layout.fillWidth: true
                        kind: "error"
                        text: workbench.controller.quickCommandOperationError
                    }

                    ListView {
                        id: quickCommandList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: workbench.filteredQuickCommands
                        keyNavigationEnabled: true
                        activeFocusOnTab: true
                        visible: workbench.filteredQuickCommands.length > 0

                        delegate: Rectangle {
                            id: commandDelegate

                            required property var modelData
                            required property int index

                            width: ListView.view.width
                            height: 64
                            radius: Theme.radiusSmall
                            color: commandDelegate.ListView.isCurrentItem ? Theme.selectedBackground : commandHover.hovered ? Theme.controlHover : "transparent"
                            border.color: commandDelegate.activeFocus ? Theme.focus : "transparent"
                            focus: commandDelegate.ListView.isCurrentItem
                            Accessible.role: Accessible.ListItem
                            Accessible.name: modelData.name
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    if ((event.modifiers & Qt.ControlModifier) !== 0) {
                                        workbench.insertRequested(commandDelegate.modelData.command);
                                    } else {
                                        workbench.runRequested(commandDelegate.modelData.command, commandDelegate);
                                    }
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Delete) {
                                    workbench.requestDeleteCommand(commandDelegate.modelData, commandDelegate);
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier) !== 0 && workbench.quickCommandIndex(commandDelegate.modelData.id) > 0) {
                                    workbench.moveQuickCommand(commandDelegate.modelData.id, -1);
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier) !== 0 && workbench.quickCommandIndex(commandDelegate.modelData.id) + 1 < workbench.controller.quickCommands.length) {
                                    workbench.moveQuickCommand(commandDelegate.modelData.id, 1);
                                    event.accepted = true;
                                }
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.right: commandActions.left
                                anchors.leftMargin: 11
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 4

                                Text {
                                    width: parent.width
                                    text: commandDelegate.modelData.name
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: commandDelegate.modelData.command.replace(/\n/g, " ↵ ")
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    width: parent.width
                                    visible: text.length > 0
                                    text: commandDelegate.modelData.description
                                    color: Theme.textSubtle
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }

                            Row {
                                id: commandActions

                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                visible: commandHover.hovered || commandDelegate.ListView.isCurrentItem || commandDelegate.activeFocus
                                spacing: 2

                                WorkbenchToolButton {
                                    id: moveCommandUpButton

                                    width: 26
                                    height: 28
                                    enabled: workbench.quickCommandIndex(commandDelegate.modelData.id) > 0
                                    onClicked: workbench.moveQuickCommand(commandDelegate.modelData.id, -1)
                                    Accessible.name: qsTr("Move code snippet up")
                                    contentItem: AppIcon {
                                        name: "chevron-up"
                                        color: moveCommandUpButton.enabled ? Theme.textSoft : Theme.textSubtle
                                    }

                                    AppToolTip {
                                        text: qsTr("Move up")
                                    }
                                }

                                WorkbenchToolButton {
                                    id: moveCommandDownButton

                                    width: 26
                                    height: 28
                                    enabled: workbench.quickCommandIndex(commandDelegate.modelData.id) + 1 < workbench.controller.quickCommands.length
                                    onClicked: workbench.moveQuickCommand(commandDelegate.modelData.id, 1)
                                    Accessible.name: qsTr("Move code snippet down")
                                    contentItem: AppIcon {
                                        name: "chevron-down"
                                        color: moveCommandDownButton.enabled ? Theme.textSoft : Theme.textSubtle
                                    }

                                    AppToolTip {
                                        text: qsTr("Move down")
                                    }
                                }

                                WorkbenchToolButton {
                                    id: runQuickCommandButton

                                    width: 26
                                    height: 28
                                    onClicked: workbench.runRequested(commandDelegate.modelData.command, runQuickCommandButton)
                                    Accessible.name: qsTr("Run code snippet")
                                    contentItem: AppIcon {
                                        name: "play"
                                        color: Theme.textSoft
                                    }

                                    AppToolTip {
                                        text: qsTr("Run")
                                    }
                                }

                                WorkbenchToolButton {
                                    width: 26
                                    height: 28
                                    onClicked: workbench.insertRequested(commandDelegate.modelData.command)
                                    Accessible.name: qsTr("Insert code snippet")
                                    contentItem: AppIcon {
                                        name: "compose"
                                        color: Theme.textSoft
                                    }

                                    AppToolTip {
                                        text: qsTr("Insert")
                                    }
                                }

                                WorkbenchToolButton {
                                    width: 26
                                    height: 28
                                    onClicked: workbench.beginEditCommand(commandDelegate.modelData)
                                    Accessible.name: qsTr("Edit code snippet")
                                    contentItem: AppIcon {
                                        name: "edit"
                                        color: Theme.textSoft
                                    }

                                    AppToolTip {
                                        text: qsTr("Edit")
                                    }
                                }

                                WorkbenchToolButton {
                                    id: deleteCommandButton

                                    width: 26
                                    height: 28
                                    onClicked: workbench.requestDeleteCommand(commandDelegate.modelData, deleteCommandButton)
                                    Accessible.name: qsTr("Delete code snippet")
                                    contentItem: AppIcon {
                                        name: "trash"
                                        color: Theme.danger
                                    }

                                    AppToolTip {
                                        text: qsTr("Delete")
                                    }
                                }
                            }

                            TapHandler {
                                onTapped: quickCommandList.currentIndex = commandDelegate.index
                            }

                            HoverHandler {
                                id: commandHover
                            }
                        }

                        ScrollBar.vertical: ScrollBar {}
                    }

                    StatePanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: workbench.filteredQuickCommands.length === 0 && !commandEditor.visible
                        kind: "empty"
                        centered: true
                        heading: quickCommandSearch.text.length > 0 ? qsTr("No matching scripts") : qsTr("No code snippets")
                        description: quickCommandSearch.text.length > 0 ? qsTr("Try a different search term.") : qsTr("Code snippets are the lightweight building blocks of the scripts library.")

                        ActionButton {
                            text: qsTr("New code snippet")
                            accessibleName: qsTr("Create the first code snippet")
                            onClicked: workbench.beginNewCommand("")
                        }
                    }
                }
            }
        }
    }

    ConfirmationDialog {
        id: deleteCommandDialog

        heading: qsTr("Delete code snippet?")
        description: qsTr("%1 will be removed from every terminal.").arg(workbench.pendingDeleteName)
        acceptText: qsTr("Delete")
        rejectText: qsTr("Cancel")
        destructive: true
        acceptObjectName: "confirmDeleteQuickCommandButton"
        rejectObjectName: "cancelDeleteQuickCommandButton"
        onAccepted: {
            workbench.controller.deleteQuickCommand(workbench.pendingDeleteId);
            close();
        }
        onClosed: {
            workbench.pendingDeleteId = "";
            workbench.pendingDeleteName = "";
        }
    }
}
