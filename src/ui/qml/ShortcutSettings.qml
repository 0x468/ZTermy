pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: pane

    required property var controller
    property string recordingActionId: ""
    property string validationError: ""
    readonly property bool recording: recordingActionId.length > 0

    spacing: Theme.spacingSection

    function filtered(category) {
        const needle = shortcutSearch.text.trim().toLocaleLowerCase();
        const result = [];
        for (let index = 0; index < controller.actions.length; ++index) {
            const action = controller.actions[index];
            if (action.category !== category) {
                continue;
            }
            const haystack = (action.label + " " + action.description + " " + action.shortcut + " " + action.id).toLocaleLowerCase();
            if (needle.length === 0 || haystack.indexOf(needle) >= 0) {
                result.push(action);
            }
        }
        return result;
    }

    function beginRecording(actionId, target) {
        recordingActionId = actionId;
        validationError = "";
        target.forceActiveFocus(Qt.ShortcutFocusReason);
    }

    function finishRecording() {
        recordingActionId = "";
    }

    component ShortcutRow: Rectangle {
        id: row

        required property var actionData
        readonly property bool compact: width < 560

        objectName: actionData.id === "application.commandPalette" ? "shortcutCommandPaletteRow" : ""
        Layout.fillWidth: true
        implicitHeight: compact ? 96 : 66
        color: "transparent"

        GridLayout {
            anchors.fill: parent
            columns: row.compact ? 1 : 2
            columnSpacing: 14
            rowSpacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: row.actionData.label
                    color: Theme.text
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                }

                Text {
                    Layout.fillWidth: true
                    text: row.actionData.description
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }
            }

            RowLayout {
                Layout.fillWidth: row.compact
                Layout.alignment: Qt.AlignRight
                spacing: 6

                Button {
                    id: recorder

                    objectName: "shortcutRecorder_" + row.actionData.id
                    Layout.fillWidth: row.compact
                    Layout.preferredWidth: 154
                    Layout.preferredHeight: 32
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: qsTr("Shortcut for %1").arg(row.actionData.label)
                    onClicked: pane.beginRecording(row.actionData.id, recorder)
                    onActiveFocusChanged: {
                        if (!activeFocus && pane.recordingActionId === row.actionData.id) {
                            pane.finishRecording();
                        }
                    }
                    Keys.onPressed: event => {
                        if (pane.recordingActionId !== row.actionData.id) {
                            return;
                        }
                        event.accepted = true;
                        if (event.key === Qt.Key_Escape) {
                            pane.finishRecording();
                            return;
                        }
                        if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta) {
                            return;
                        }
                        const result = event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete ? pane.controller.setActionShortcut(row.actionData.id, "") : pane.controller.setActionShortcutFromKey(row.actionData.id, event.key, event.modifiers);
                        if (result.valid) {
                            pane.validationError = "";
                            pane.finishRecording();
                        } else {
                            pane.validationError = result.error;
                        }
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: pane.recordingActionId === row.actionData.id ? qsTr("Press shortcut…") : (row.actionData.shortcut.length > 0 ? row.actionData.shortcut : qsTr("Unbound"))
                        color: pane.recordingActionId === row.actionData.id ? Theme.accent : Theme.textSoft
                        elide: Text.ElideRight
                        font.family: Theme.terminalFont
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: recorder.down ? Theme.controlPressed : recorder.hovered ? Theme.controlHover : Theme.fieldBackground
                        border.color: pane.recordingActionId === row.actionData.id ? Theme.accent : recorder.activeFocus ? Theme.focus : Theme.border
                        border.width: pane.recordingActionId === row.actionData.id || recorder.activeFocus ? 2 : 1
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                ToolButton {
                    id: clearAction

                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    enabled: row.actionData.shortcut.length > 0
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: qsTr("Unbind %1").arg(row.actionData.label)
                    onClicked: {
                        const result = pane.controller.setActionShortcut(row.actionData.id, "");
                        pane.validationError = result.valid ? "" : result.error;
                    }
                    contentItem: Text {
                        text: "×"
                        color: clearAction.enabled ? Theme.textMuted : Theme.textSubtle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.uiFont
                        font.pixelSize: 18
                    }
                    background: Rectangle {
                        radius: 15
                        color: clearAction.down ? Theme.controlPressed : clearAction.hovered ? Theme.controlHover : "transparent"
                        border.color: clearAction.activeFocus ? Theme.focus : "transparent"
                        border.width: clearAction.activeFocus ? 2 : 0
                    }
                    AppToolTip {
                        visible: clearAction.hovered
                        text: qsTr("Unbind shortcut")
                    }
                }

                ToolButton {
                    id: resetAction

                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    enabled: row.actionData.customized
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: qsTr("Reset %1 to its default shortcut").arg(row.actionData.label)
                    onClicked: {
                        if (!pane.controller.resetActionShortcut(row.actionData.id)) {
                            pane.validationError = qsTr("The shortcut could not be reset.");
                        } else {
                            pane.validationError = "";
                        }
                    }
                    contentItem: AppIcon {
                        name: "refresh"
                        color: resetAction.enabled ? Theme.textMuted : Theme.textSubtle
                    }
                    background: Rectangle {
                        radius: 15
                        color: resetAction.down ? Theme.controlPressed : resetAction.hovered ? Theme.controlHover : "transparent"
                        border.color: resetAction.activeFocus ? Theme.focus : "transparent"
                        border.width: resetAction.activeFocus ? 2 : 0
                    }
                    AppToolTip {
                        visible: resetAction.hovered
                        text: qsTr("Reset to default")
                    }
                }
            }
        }
    }

    component ShortcutGroup: SectionCard {
        id: group

        required property string category
        required property string title
        readonly property var entries: pane.filtered(category)

        Layout.fillWidth: true
        visible: entries.length > 0
        heading: title

        Repeater {
            model: group.entries

            ShortcutRow {
                required property var modelData
                actionData: modelData
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        AppTextField {
            id: shortcutSearch

            objectName: "shortcutSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Search actions and shortcuts")
            accessibleName: qsTr("Search keyboard shortcuts")
            onTextEdited: {
                pane.finishRecording();
                pane.validationError = "";
            }
        }

        ActionButton {
            objectName: "resetAllShortcuts"
            text: qsTr("Reset all")
            iconName: "refresh"
            accessibleName: qsTr("Reset all keyboard shortcuts")
            onClicked: {
                pane.finishRecording();
                pane.validationError = pane.controller.resetAllActionShortcuts() ? "" : qsTr("The shortcuts could not be reset.");
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: pane.recording || pane.validationError.length > 0
        text: pane.validationError.length > 0 ? pane.validationError : qsTr("Press a shortcut. Escape cancels; Backspace or Delete clears the binding.")
        color: pane.validationError.length > 0 ? Theme.dangerText : Theme.textMuted
        wrapMode: Text.WordWrap
        font.family: Theme.uiFont
        font.pixelSize: Theme.textCompact
    }

    ShortcutGroup {
        category: "application"
        title: qsTr("Application")
    }

    ShortcutGroup {
        category: "tabs"
        title: qsTr("Tabs")
    }

    ShortcutGroup {
        category: "terminal"
        title: qsTr("Terminal")
    }

    Text {
        Layout.fillWidth: true
        visible: pane.filtered("application").length + pane.filtered("tabs").length + pane.filtered("terminal").length === 0
        text: qsTr("No matching shortcuts")
        color: Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
    }
}
