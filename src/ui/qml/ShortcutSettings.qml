pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: pane

    required property var controller
    required property var windowChrome
    property string recordingActionId: ""
    property int recordingIndex: 0
    property string validationError: ""
    readonly property bool recording: recordingActionId.length > 0
    readonly property var globalAction: ({
            id: "system.summon",
            label: qsTr("Show or hide the main window"),
            description: qsTr("System-wide, including when another application is focused"),
            shortcut: controller.windowInteractionSettings.globalShortcut || "",
            customized: (controller.windowInteractionSettings.globalShortcut || "").length > 0
        })
    readonly property bool globalMatches: matches(globalAction)

    spacing: Theme.spacingSection

    function matches(action) {
        const needle = shortcutSearch.text.trim().toLocaleLowerCase();
        const haystack = (action.label + " " + action.description + " " + (action.shortcuts || [action.shortcut]).join(" ") + " " + action.id).toLocaleLowerCase();
        return needle.length === 0 || haystack.indexOf(needle) >= 0;
    }

    function globalResult(saved) {
        return {
            valid: saved,
            error: saved ? "" : windowChrome.globalShortcutError || qsTr("The shortcut could not be saved.")
        };
    }

    function filtered(category) {
        const result = [];
        for (let index = 0; index < controller.actions.length; ++index) {
            const action = controller.actions[index];
            if (action.category !== category) {
                continue;
            }
            if (matches(action)) {
                result.push(action);
            }
        }
        return result;
    }

    function beginRecording(actionId, target, index = 0) {
        recordingActionId = actionId;
        recordingIndex = index;
        validationError = "";
        target.forceActiveFocus(Qt.ShortcutFocusReason);
    }

    function finishRecording() {
        recordingActionId = "";
    }

    function recordKey(action, systemWide, index, event) {
        if (recordingActionId !== action.id || recordingIndex !== index)
            return;
        event.accepted = true;
        if (event.key === Qt.Key_Escape) {
            finishRecording();
            return;
        }
        if ([Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].includes(event.key))
            return;
        const clear = event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete;
        let result;
        if (systemWide) {
            result = globalResult(clear ? controller.saveWindowInteractionSettings({
                globalShortcut: ""
            }) : controller.setGlobalShortcutFromKey(event.key, event.modifiers));
        } else if (clear) {
            const bindings = action.shortcuts.slice();
            if (index >= 0)
                bindings.splice(index, 1);
            result = controller.setActionShortcuts(action.id, bindings);
        } else {
            result = controller.editActionShortcutFromKey(action.id, index, event.key, event.modifiers);
        }
        validationError = result.valid ? "" : result.error;
        if (result.valid)
            finishRecording();
    }

    component ShortcutRow: Rectangle {
        id: row

        required property var actionData
        property bool systemWide: false
        readonly property bool compact: width < 560
        readonly property var bindings: systemWide ? [actionData.shortcut] : actionData.shortcuts.length > 0 ? actionData.shortcuts : [""]

        objectName: actionData.id === "application.commandPalette" ? "shortcutCommandPaletteRow" : ""
        Layout.fillWidth: true
        implicitHeight: Math.max(66, shortcutGrid.implicitHeight + 12)
        color: "transparent"

        GridLayout {
            id: shortcutGrid
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

            ColumnLayout {
                Layout.fillWidth: row.compact
                Layout.alignment: Qt.AlignRight
                spacing: 6

                Repeater {
                    model: row.bindings
                    RowLayout {
                        id: bindingRow
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Button {
                            id: recorder

                            objectName: "shortcutRecorder_" + row.actionData.id + (bindingRow.index === 0 ? "" : "_" + bindingRow.index)
                            Layout.fillWidth: row.compact
                            Layout.preferredWidth: 154
                            Layout.preferredHeight: 32
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: qsTr("Shortcut for %1").arg(row.actionData.label)
                            onClicked: pane.beginRecording(row.actionData.id, recorder, bindingRow.index)
                            onActiveFocusChanged: {
                                if (!activeFocus && pane.recordingActionId === row.actionData.id && pane.recordingIndex === bindingRow.index) {
                                    pane.finishRecording();
                                }
                            }
                            Keys.onPressed: event => pane.recordKey(row.actionData, row.systemWide, bindingRow.index, event)

                            contentItem: Text {
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: pane.recordingActionId === row.actionData.id && pane.recordingIndex === bindingRow.index ? qsTr("Press shortcut…") : (bindingRow.modelData.length > 0 ? bindingRow.modelData : qsTr("Unbound"))
                                color: pane.recordingActionId === row.actionData.id && pane.recordingIndex === bindingRow.index ? Theme.accent : Theme.textSoft
                                elide: Text.ElideRight
                                font.family: Theme.terminalFont
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            background: Rectangle {
                                radius: Theme.radiusControl
                                color: recorder.down ? Theme.controlPressed : recorder.hovered ? Theme.controlHover : Theme.fieldBackground
                                border.color: pane.recordingActionId === row.actionData.id && pane.recordingIndex === bindingRow.index ? Theme.accent : recorder.visualFocus ? Theme.focus : Theme.border
                                border.width: pane.recordingActionId === row.actionData.id && pane.recordingIndex === bindingRow.index || recorder.visualFocus ? 2 : 1
                            }

                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }
                        }

                        ToolButton {
                            id: clearAction

                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            enabled: bindingRow.modelData.length > 0
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            Accessible.name: qsTr("Unbind %1").arg(row.actionData.label)
                            onClicked: {
                                const bindings = row.systemWide ? [] : row.actionData.shortcuts.slice();
                                if (!row.systemWide)
                                    bindings.splice(bindingRow.index, 1);
                                const result = row.systemWide ? pane.globalResult(pane.controller.saveWindowInteractionSettings({
                                    globalShortcut: ""
                                })) : pane.controller.setActionShortcuts(row.actionData.id, bindings);
                                pane.validationError = result.valid ? "" : result.error;
                            }
                            contentItem: AppIcon {
                                name: "close"
                                color: clearAction.enabled ? Theme.textMuted : Theme.textSubtle
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: clearAction.down ? Theme.controlPressed : clearAction.hovered ? Theme.controlHover : "transparent"
                                border.color: clearAction.visualFocus ? Theme.focus : "transparent"
                                border.width: clearAction.visualFocus ? 2 : 0
                            }
                            AppToolTip {
                                visible: clearAction.hovered
                                text: qsTr("Unbind shortcut")
                            }
                        }

                        ToolButton {
                            id: resetAction
                            visible: bindingRow.index === 0

                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            enabled: row.actionData.customized
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            Accessible.name: qsTr("Reset %1 to its default shortcut").arg(row.actionData.label)
                            onClicked: {
                                const result = row.systemWide ? pane.globalResult(pane.controller.saveWindowInteractionSettings({
                                    globalShortcut: ""
                                })) : {
                                    valid: pane.controller.resetActionShortcut(row.actionData.id),
                                    error: qsTr("The shortcut could not be reset.")
                                };
                                pane.validationError = result.valid ? "" : result.error;
                            }
                            contentItem: AppIcon {
                                name: "refresh"
                                color: resetAction.enabled ? Theme.textMuted : Theme.textSubtle
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: resetAction.down ? Theme.controlPressed : resetAction.hovered ? Theme.controlHover : "transparent"
                                border.color: resetAction.visualFocus ? Theme.focus : "transparent"
                                border.width: resetAction.visualFocus ? 2 : 0
                            }
                            AppToolTip {
                                visible: resetAction.hovered
                                text: qsTr("Reset to default")
                            }
                        }
                    }
                }
                ActionButton {
                    objectName: "shortcutAdd_" + row.actionData.id
                    visible: !row.systemWide
                    enabled: !row.systemWide && row.actionData.shortcuts.length < 8
                    text: pane.recordingActionId === row.actionData.id && pane.recordingIndex === -1 ? qsTr("Press shortcut…") : qsTr("Add shortcut")
                    iconName: "plus"
                    onClicked: pane.beginRecording(row.actionData.id, this, -1)
                    onActiveFocusChanged: {
                        if (!activeFocus && pane.recordingActionId === row.actionData.id && pane.recordingIndex === -1)
                            pane.finishRecording();
                    }
                    Keys.onPressed: event => pane.recordKey(row.actionData, false, -1, event)
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

    SectionCard {
        objectName: "systemShortcutSettingsCard"
        Layout.fillWidth: true
        visible: pane.globalMatches
        heading: qsTr("System-wide")

        ShortcutRow {
            actionData: pane.globalAction
            systemWide: true
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Hides only when the main window is in front; otherwise restores and focuses it. Detached windows are unchanged. Clear the shortcut to disable it.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
        }
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
        visible: !pane.globalMatches && pane.filtered("application").length + pane.filtered("tabs").length + pane.filtered("terminal").length === 0
        text: qsTr("No matching shortcuts")
        color: Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
    }
}
