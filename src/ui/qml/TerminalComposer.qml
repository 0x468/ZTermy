pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: composer

    required property var controller
    required property var activeTab
    property real panelHeight: 132
    property real dragStartGlobalY: 0
    property real dragStartHeight: panelHeight
    property string currentTabId: ""
    property var drafts: ({})

    signal heightRequested(real height)
    signal closeRequested

    component ComposerToolButton: ToolButton {
        id: control

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

        background: Rectangle {
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

        HoverHandler {
            cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    color: Theme.panelBackground
    border.color: Theme.border
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Command composer")

    onActiveTabChanged: {
        if (currentTabId.length > 0) {
            drafts[currentTabId] = composerInput.text;
        }
        currentTabId = activeTab ? activeTab.id : "";
        composerInput.text = currentTabId.length > 0 && drafts[currentTabId] !== undefined ? drafts[currentTabId] : "";
    }

    function focusEditor() {
        Qt.callLater(composerInput.forceActiveFocus);
    }

    function insertSnippet(command) {
        const insertionPoint = composerInput.cursorPosition;
        composerInput.insert(insertionPoint, command);
        composerInput.cursorPosition = insertionPoint + command.length;
        focusEditor();
    }

    function sendCommand(command, clearEditor) {
        if (!command || command.trim().length === 0) {
            return false;
        }
        if (!controller.runTerminalCommand(command)) {
            return false;
        }
        if (clearEditor) {
            composerInput.clear();
            if (currentTabId.length > 0) {
                drafts[currentTabId] = "";
            }
        }
        focusEditor();
        return true;
    }

    function snippetTooltip(snippet) {
        const detail = snippet.description && snippet.description.length > 0 ? "\n" + snippet.description : "";
        return snippet.name + detail + "\n\n" + snippet.command + "\n\n" + qsTr("Click to insert · Shift+click to send");
    }

    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6
        z: 10
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: mouse => {
            composer.dragStartGlobalY = mapToGlobal(mouse.x, mouse.y).y;
            composer.dragStartHeight = composer.panelHeight;
        }
        onPositionChanged: mouse => {
            if (!pressed) {
                return;
            }
            const globalY = mapToGlobal(mouse.x, mouse.y).y;
            composer.heightRequested(Math.max(92, Math.min(360, composer.dragStartHeight + composer.dragStartGlobalY - globalY)));
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        anchors.bottomMargin: 6
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 4

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                orientation: ListView.Horizontal
                spacing: 4
                clip: true
                model: composer.controller.quickCommands

                delegate: Button {
                    id: snippetChip

                    required property var modelData

                    height: 24
                    leftPadding: 8
                    rightPadding: 8
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: qsTr("Code snippet %1").arg(modelData.name)
                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !event.isAutoRepeat) {
                            if ((event.modifiers & Qt.ShiftModifier) !== 0) {
                                composer.sendCommand(snippetChip.modelData.command, false);
                            } else {
                                composer.insertSnippet(snippetChip.modelData.command);
                            }
                            event.accepted = true;
                        }
                    }
                    contentItem: Text {
                        text: snippetChip.modelData.name
                        color: Theme.textSoft
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                    background: Rectangle {
                        radius: height / 2
                        color: snippetMouse.pressed ? Theme.controlPressed : snippetMouse.containsMouse ? Theme.controlHover : "transparent"
                        border.color: snippetChip.activeFocus ? Theme.focus : snippetMouse.containsMouse ? Theme.borderStrong : Theme.border
                        border.width: snippetChip.activeFocus ? 2 : 1

                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.motionFast
                            }
                        }
                    }

                    AppToolTip {
                        hoverTarget: snippetMouse
                        delay: 350
                        text: composer.snippetTooltip(snippetChip.modelData)
                    }

                    MouseArea {
                        id: snippetMouse

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: snippetChip.forceActiveFocus()
                        onClicked: mouse => {
                            if ((mouse.modifiers & Qt.ShiftModifier) !== 0) {
                                composer.sendCommand(snippetChip.modelData.command, false);
                            } else {
                                composer.insertSnippet(snippetChip.modelData.command);
                            }
                        }
                    }
                }
            }

            ComposerToolButton {
                objectName: "manageComposerQuickCommandsButton"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                onClicked: composer.controller.toggleTerminalWorkbench("scripts")
                Accessible.name: qsTr("Manage scripts and code snippets")
                contentItem: AppIcon {
                    name: "commands"
                    color: Theme.textSoft
                }

                AppToolTip {
                    text: qsTr("Scripts library")
                }
            }

            ComposerToolButton {
                objectName: "closeTerminalComposerButton"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                onClicked: composer.closeRequested()
                Accessible.name: qsTr("Close command composer")
                contentItem: AppIcon {
                    name: "close"
                    color: Theme.textSoft
                }

                AppToolTip {
                    text: qsTr("Close")
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: composerInput

                objectName: "terminalComposerInput"
                placeholderText: qsTr("Enter a command. Enter sends · Shift+Enter adds a new line")
                color: Theme.text
                placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentText
                wrapMode: TextEdit.WrapAnywhere
                font.family: Theme.terminalFont
                font.pixelSize: Theme.textBody
                Accessible.name: qsTr("Command composer input")
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier) === 0 && !composerInput.inputMethodComposing) {
                        composer.sendCommand(text, true);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Escape) {
                        composer.closeRequested();
                        event.accepted = true;
                    }
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: "transparent"
                    border.color: composerInput.activeFocus ? Theme.focus : "transparent"
                }
            }
        }
    }
}
