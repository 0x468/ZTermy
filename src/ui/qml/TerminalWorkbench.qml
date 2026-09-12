pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SidePanelSurface {
    id: workbench

    required property var controller
    required property var activeTab
    required property string panelSide
    property real panelWidth: 360
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string appliedHistorySearch: ""
    property string scriptSurface: "library"
    readonly property string currentPage: activeTab ? activeTab.workbenchPage : "history"
    readonly property var historySource: controller.terminalHistory
    readonly property string historySessionId: activeTab ? activeTab.sessionId || "" : ""
    readonly property bool historySessionRunning: !!activeTab && !!activeTab.running
    property string presentedHistorySession: ""
    property string presentedHistorySearch: ""
    property int historyPresentationRevision: 0
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
    onFilteredHistoryChanged: Qt.callLater(syncHistoryModel)

    signal panelWidthRequested(real width)
    signal panelResizeStarted
    signal panelResizeFinished
    signal insertRequested(string command)
    signal runRequested(string command, var sourceItem)
    signal importLibraryRequested
    signal exportLibraryRequested
    signal aiSettingsRequested
    signal closeRequested

    Timer {
        interval: 1000
        running: workbench.visible && workbench.currentPage === "history"
        repeat: true
        onTriggered: workbench.controller.refreshSessionHistory()
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
            border.color: scopeControl.visualFocus ? Theme.focus : "transparent"
            border.width: scopeControl.visualFocus ? 2 : 0
        }

        HoverHandler {
            cursorShape: scopeControl.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    focus: visible
    Keys.onEscapePressed: closeRequested()
    panelTitle: currentPage === "sftp" ? qsTr("Remote files") : currentPage === "history" ? qsTr("Command history") : currentPage === "notes" ? qsTr("Notes") : currentPage === "ai" ? qsTr("Terminal AI assistant") : qsTr("Scripts")
    onVisibleChanged: {
        Qt.callLater(ensureHistoryLoaded);
    }
    onHistorySessionIdChanged: {
        Qt.callLater(ensureHistoryLoaded);
        Qt.callLater(syncHistoryModel);
    }
    onHistorySessionRunningChanged: Qt.callLater(ensureHistoryLoaded)
    Component.onCompleted: {
        Qt.callLater(ensureHistoryLoaded);
        Qt.callLater(syncHistoryModel);
    }
    onCurrentPageChanged: {
        if (currentPage !== "scripts") {
            scriptSurface = "library";
        }
        Qt.callLater(ensureHistoryLoaded);
    }

    function ensureHistoryLoaded() {
        if (visible && currentPage === "history" && historySessionRunning)
            controller.refreshTerminalHistory();
    }

    function syncHistoryModel() {
        replaceHistoryEntries(filteredHistory, historySessionId, appliedHistorySearch);
    }

    function replaceHistoryEntries(entries, sessionId, search) {
        if (!historyList)
            return;
        const sameContext = presentedHistorySession === sessionId && presentedHistorySearch === search;
        const previous = historyList.model || [];
        const rowHeight = 48 + historyList.spacing;
        const topIndex = Math.max(0, Math.floor((historyList.contentY - historyList.originY) / rowHeight));
        const topCommand = sameContext && topIndex < previous.length ? previous[topIndex].command : "";
        const selectedCommand = sameContext && historyList.currentIndex >= 0 && historyList.currentIndex < previous.length ? previous[historyList.currentIndex].command : "";
        const offset = sameContext ? historyList.contentY - historyList.originY - topIndex * rowHeight : 0;
        presentedHistorySession = sessionId;
        presentedHistorySearch = search;
        const revision = ++historyPresentationRevision;
        historyList.model = entries;
        historyList.currentIndex = entries.findIndex(entry => entry.command === selectedCommand);
        Qt.callLater(() => {
            if (revision !== historyPresentationRevision)
                return;
            historyList.forceLayout();
            const restoredIndex = topCommand.length > 0 ? entries.findIndex(entry => entry.command === topCommand) : -1;
            const desiredY = restoredIndex >= 0 ? restoredIndex * rowHeight + offset : sameContext ? topIndex * rowHeight + offset : 0;
            const maximumY = Math.max(0, historyList.contentHeight - historyList.height);
            historyList.contentY = historyList.originY + Math.max(0, Math.min(maximumY, desiredY));
        });
    }

    function beginNewCommand(prefill) {
        scriptEditor.beginNew(prefill || "");
        scriptSurface = "editor";
    }

    function saveHistoryCommand(command) {
        controller.toggleTerminalWorkbench("scripts");
        Qt.callLater(() => beginNewCommand(command));
    }

    function beginEditCommand(command) {
        scriptEditor.beginEdit(command);
        scriptSurface = "editor";
    }

    function beginRunScript(command) {
        scriptRunPane.begin(command);
        scriptSurface = "run";
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

    ResizeGrip {
        id: resizeHandle

        objectName: "terminalWorkbenchResizeHandle"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 7
        x: workbench.panelSide === "right" ? 0 : parent.width - width
        z: 20
        value: workbench.panelWidth
        minimum: 320
        maximum: 800
        defaultValue: 520
        direction: workbench.panelSide === "left" ? 1 : -1
        snapPoints: [400, 520, 640]
        onPressedChanged: {
            if (pressed)
                workbench.panelResizeStarted();
            else
                workbench.panelResizeFinished();
        }
        onValueEdited: value => workbench.panelWidthRequested(value)
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
            Layout.preferredHeight: 34
            color: Theme.chromeBackground

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 4
                spacing: 4

                AppIconButton {
                    id: sftpPageButton
                    objectName: "terminalRemoteFilesPageButton"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    checkable: true
                    checked: workbench.currentPage === "sftp"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("sftp")
                    label: qsTr("Remote files")
                    iconName: "folder"
                    iconColor: sftpPageButton.checked ? Theme.accent : Theme.textSoft

                    toolTipText: qsTr("Remote files")
                }

                AppIconButton {
                    id: historyPageButton
                    objectName: "terminalHistoryPageButton"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    checkable: true
                    checked: workbench.currentPage === "history"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("history")
                    label: qsTr("Command history")
                    iconName: "history"
                    iconColor: historyPageButton.checked ? Theme.accent : Theme.textSoft
                    toolTipText: qsTr("Command history")
                }

                AppIconButton {
                    id: quickCommandsPageButton
                    objectName: "terminalScriptsPageButton"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    checkable: true
                    checked: workbench.currentPage === "scripts"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("scripts")
                    label: qsTr("Scripts")
                    iconName: "commands"
                    iconColor: quickCommandsPageButton.checked ? Theme.accent : Theme.textSoft
                    toolTipText: qsTr("Scripts")
                }

                AppIconButton {
                    id: notesPageButton
                    objectName: "terminalNotesPageButton"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    checkable: true
                    checked: workbench.currentPage === "notes"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("notes")
                    label: qsTr("Notes")
                    iconName: "file"
                    iconColor: notesPageButton.checked ? Theme.accent : Theme.textSoft

                    toolTipText: qsTr("Notes")
                }

                AppIconButton {
                    id: aiPageButton

                    objectName: "terminalAiAssistantButton"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    checkable: true
                    checked: workbench.currentPage === "ai"
                    selected: checked
                    onClicked: workbench.controller.toggleTerminalWorkbench("ai")
                    label: qsTr("Terminal AI assistant")
                    iconName: "ai"
                    iconColor: aiPageButton.checked ? Theme.accent : Theme.textSoft

                    toolTipText: qsTr("AI assistant")
                }

                Item {
                    Layout.fillWidth: true
                }

                AppIconButton {
                    objectName: "moveTerminalWorkbenchButton"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    onClicked: workbench.controller.moveTerminalWorkbench()
                    label: workbench.panelSide === "left" ? qsTr("Move terminal workbench right") : qsTr("Move terminal workbench left")
                    iconName: "swap-horizontal"
                    iconColor: Theme.textSoft
                }

                AppIconButton {
                    objectName: "closeTerminalWorkbenchButton"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    onClicked: workbench.closeRequested()
                    label: qsTr("Close terminal workbench")
                    iconName: "close"
                    iconColor: Theme.textSoft
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

                NotesPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: workbench.currentPage === "notes"
                    controller: workbench.controller
                }

                AiAssistantPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: workbench.currentPage === "ai"
                    controller: workbench.controller
                    activeTab: workbench.activeTab
                    onSettingsRequested: workbench.aiSettingsRequested()
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

                            AppIconButton {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: workbench.controller.terminalHistoryState !== "loading"
                                onClicked: workbench.controller.refreshTerminalHistory()
                                label: qsTr("Refresh command history")
                                iconName: "history"
                                iconColor: Theme.text
                                rotation: parent.enabled ? 0 : 180

                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: Theme.motionMedium
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            spacing: 3

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Session + Shell history file")
                                color: Theme.textSubtle
                                font.pixelSize: Theme.textCompact
                                elide: Text.ElideRight
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
                            text: workbench.controller.terminalHistoryError
                        }

                        ListView {
                            id: historyList

                            objectName: "terminalHistoryList"

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: count > 0
                            clip: true
                            spacing: 1
                            model: []
                            keyNavigationEnabled: true
                            activeFocusOnTab: true

                            delegate: Rectangle {
                                id: historyDelegate

                                required property var modelData
                                required property int index

                                width: ListView.view.width
                                height: 48
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
                                    visible: (historyDelegate.modelData.sourceLabel || "").length > 0
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

                                    AppIconButton {
                                        id: runHistoryCommandButton

                                        width: 26
                                        height: 26
                                        onClicked: workbench.runRequested(historyDelegate.modelData.command, runHistoryCommandButton)
                                        label: qsTr("Run history command")
                                        iconName: "play"
                                        iconColor: Theme.textSoft

                                        toolTipText: qsTr("Run")
                                    }

                                    AppIconButton {
                                        width: 26
                                        height: 26
                                        onClicked: workbench.insertRequested(historyDelegate.modelData.command)
                                        label: qsTr("Insert history command")
                                        iconName: "compose"
                                        iconColor: Theme.textSoft

                                        toolTipText: qsTr("Insert")
                                    }

                                    AppIconButton {
                                        width: 26
                                        height: 26
                                        onClicked: workbench.saveHistoryCommand(historyDelegate.modelData.command)
                                        label: qsTr("Save history command")
                                        iconName: "save"
                                        iconColor: Theme.textSoft

                                        toolTipText: qsTr("Save as script")
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
                            visible: historyList.count === 0
                            kind: workbench.controller.terminalHistoryState === "loading" ? "loading" : "empty"
                            centered: true
                            heading: workbench.controller.terminalHistoryState === "loading" ? qsTr("Loading history") : historySearch.text.length > 0 ? qsTr("No matching history") : qsTr("No command history")
                            description: workbench.controller.terminalHistoryState === "loading" ? qsTr("Reading a bounded snapshot outside the interface thread.") : historySearch.text.length > 0 ? qsTr("Try a different search term.") : qsTr("Only this session and its Shell history file are shown. Nothing is saved by ztermy.")

                            ActionButton {
                                text: qsTr("Refresh history")
                                visible: workbench.controller.terminalHistoryState !== "loading"
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
                        visible: workbench.scriptSurface === "library"
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            name: "commands"
                            color: Theme.accent
                        }

                        Text {
                            text: qsTr("Scripts")
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

                        AppIconButton {
                            id: scriptLibraryMenuButton

                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 26
                            onClicked: scriptLibraryMenu.open()
                            label: qsTr("Script library actions")
                            iconName: "more"
                            iconColor: Theme.textSoft
                            toolTipEnabled: !scriptLibraryMenu.visible

                            AppMenu {
                                id: scriptLibraryMenu

                                y: scriptLibraryMenuButton.height

                                AppMenuItem {
                                    text: qsTr("Import library")
                                    onTriggered: workbench.importLibraryRequested()
                                }

                                AppMenuItem {
                                    text: qsTr("Export library")
                                    enabled: workbench.controller.quickCommands.length > 0
                                    onTriggered: workbench.exportLibraryRequested()
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: workbench.scriptSurface === "library"
                        spacing: 8

                        AppTextField {
                            id: quickCommandSearch

                            Layout.fillWidth: true
                            compact: true
                            placeholderText: qsTr("Search scripts")
                            accessibleName: qsTr("Search scripts")
                        }

                        AppIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            onClicked: workbench.beginNewCommand("")
                            label: qsTr("New script")
                            iconName: "plus"
                            iconColor: Theme.text
                        }
                    }

                    ScriptEditor {
                        id: scriptEditor

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: workbench.scriptSurface === "editor"
                        controller: workbench.controller
                        onClosed: workbench.scriptSurface = "library"
                        onSaved: workbench.scriptSurface = "library"
                    }

                    ScriptRunPane {
                        id: scriptRunPane

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: workbench.scriptSurface === "run"
                        controller: workbench.controller
                        activeTab: workbench.activeTab
                        onClosed: workbench.scriptSurface = "library"
                    }

                    StatusMessage {
                        Layout.fillWidth: true
                        visible: workbench.scriptSurface === "library"
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
                        visible: workbench.scriptSurface === "library" && workbench.filteredQuickCommands.length > 0

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
                                        workbench.beginRunScript(commandDelegate.modelData);
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

                                AppIconButton {
                                    id: moveCommandUpButton

                                    width: 26
                                    height: 28
                                    enabled: workbench.quickCommandIndex(commandDelegate.modelData.id) > 0
                                    onClicked: workbench.moveQuickCommand(commandDelegate.modelData.id, -1)
                                    label: qsTr("Move script up")
                                    iconName: "chevron-up"
                                    iconColor: moveCommandUpButton.enabled ? Theme.textSoft : Theme.textSubtle

                                    toolTipText: qsTr("Move up")
                                }

                                AppIconButton {
                                    id: moveCommandDownButton

                                    width: 26
                                    height: 28
                                    enabled: workbench.quickCommandIndex(commandDelegate.modelData.id) + 1 < workbench.controller.quickCommands.length
                                    onClicked: workbench.moveQuickCommand(commandDelegate.modelData.id, 1)
                                    label: qsTr("Move script down")
                                    iconName: "chevron-down"
                                    iconColor: moveCommandDownButton.enabled ? Theme.textSoft : Theme.textSubtle

                                    toolTipText: qsTr("Move down")
                                }

                                AppIconButton {
                                    id: runQuickCommandButton

                                    width: 26
                                    height: 28
                                    onClicked: workbench.beginRunScript(commandDelegate.modelData)
                                    label: qsTr("Review and run script")
                                    iconName: "play"
                                    iconColor: Theme.textSoft

                                    toolTipText: qsTr("Run")
                                }

                                AppIconButton {
                                    width: 26
                                    height: 28
                                    onClicked: workbench.insertRequested(commandDelegate.modelData.command)
                                    label: qsTr("Insert script text")
                                    iconName: "compose"
                                    iconColor: Theme.textSoft

                                    toolTipText: qsTr("Insert")
                                }

                                AppIconButton {
                                    width: 26
                                    height: 28
                                    onClicked: workbench.beginEditCommand(commandDelegate.modelData)
                                    label: qsTr("Edit script")
                                    iconName: "edit"
                                    iconColor: Theme.textSoft

                                    toolTipText: qsTr("Edit")
                                }

                                AppIconButton {
                                    id: deleteCommandButton

                                    width: 26
                                    height: 28
                                    onClicked: workbench.requestDeleteCommand(commandDelegate.modelData, deleteCommandButton)
                                    label: qsTr("Delete script")
                                    iconName: "trash"
                                    iconColor: Theme.danger

                                    toolTipText: qsTr("Delete")
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
                        visible: workbench.scriptSurface === "library" && workbench.filteredQuickCommands.length === 0
                        kind: "empty"
                        centered: true
                        heading: quickCommandSearch.text.length > 0 ? qsTr("No matching scripts") : qsTr("No scripts")
                        description: quickCommandSearch.text.length > 0 ? qsTr("Try a different search term.") : qsTr("Build reusable, typed command sequences and run them against one explicit terminal.")

                        ActionButton {
                            text: qsTr("New script")
                            accessibleName: qsTr("Create the first script")
                            onClicked: workbench.beginNewCommand("")
                        }
                    }
                }
            }
        }
    }

    ConfirmationDialog {
        id: deleteCommandDialog

        heading: qsTr("Delete script?")
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
