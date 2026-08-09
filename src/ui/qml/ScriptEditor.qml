pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var controller
    property string editingId: ""

    signal closed
    signal saved

    function beginNew(prefill) {
        editingId = "";
        scriptName.text = prefill && prefill.length > 0 ? prefill.split("\n")[0].slice(0, 64) : "";
        scriptDescription.text = "";
        scriptShell.currentIndex = 0;
        variableModel.clear();
        stepModel.clear();
        stepModel.append({
            "command": prefill || "",
            "continuation": "immediate",
            "outputMarker": "",
            "timeoutMs": "30000"
        });
        Qt.callLater(scriptName.forceActiveFocus);
    }

    function beginEdit(script) {
        editingId = script.id;
        scriptName.text = script.name;
        scriptDescription.text = script.description;
        scriptShell.currentIndex = script.shell === "posix" ? 1 : script.shell === "powershell" ? 2 : 0;
        variableModel.clear();
        for (const variable of script.variables || []) {
            variableModel.append({
                "name": variable.name,
                "label": variable.label,
                "type": variable.type,
                "defaultValue": variable.defaultValue,
                "choicesText": (variable.choices || []).join(", "),
                "required": variable.required
            });
        }
        stepModel.clear();
        for (const step of script.steps || []) {
            stepModel.append({
                "command": step.command,
                "continuation": step.continuation,
                "outputMarker": step.outputMarker,
                "timeoutMs": String(step.timeoutMs)
            });
        }
        Qt.callLater(scriptName.forceActiveFocus);
    }

    function save() {
        const variables = [];
        for (let index = 0; index < variableModel.count; ++index) {
            const variable = variableModel.get(index);
            const choices = variable.type === "choice" ? variable.choicesText.split(",").map(value => value.trim()).filter(value => value.length > 0) : [];
            variables.push({
                "name": variable.name,
                "label": variable.label,
                "type": variable.type,
                "defaultValue": variable.defaultValue,
                "choices": choices,
                "required": variable.required
            });
        }
        const steps = [];
        for (let index = 0; index < stepModel.count; ++index) {
            const step = stepModel.get(index);
            steps.push({
                "command": step.command,
                "continuation": step.continuation,
                "outputMarker": step.continuation === "literal-output" ? step.outputMarker : "",
                "timeoutMs": Number(step.timeoutMs)
            });
        }
        const shell = scriptShell.currentIndex === 1 ? "posix" : scriptShell.currentIndex === 2 ? "powershell" : "any";
        if (controller.saveScript({
            "id": editingId,
            "name": scriptName.text,
            "description": scriptDescription.text,
            "shell": shell,
            "variables": variables,
            "steps": steps
        })) {
            saved();
        }
    }

    readonly property bool canSave: scriptName.text.trim().length > 0 && stepModel.count > 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ToolButton {
                id: backButton

                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                hoverEnabled: true
                onClicked: root.closed()
                Accessible.name: qsTr("Back to script library")
                contentItem: AppIcon {
                    name: "chevron-left"
                    color: Theme.text
                }
                background: Rectangle {
                    radius: height / 2
                    color: backButton.down ? Theme.controlPressed : backButton.hovered ? Theme.controlHover : "transparent"
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.editingId.length > 0 ? qsTr("Edit script") : qsTr("New script")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
                font.weight: Font.DemiBold
            }

            ActionButton {
                text: qsTr("Save")
                variant: "primary"
                enabled: root.canSave
                onClicked: root.save()
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: Math.max(0, parent.width - 12)
                spacing: 10

                Text {
                    text: qsTr("Details")
                    color: Theme.textSoft
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.DemiBold
                }

                AppTextField {
                    id: scriptName

                    Layout.fillWidth: true
                    compact: true
                    placeholderText: qsTr("Script name")
                    accessibleName: qsTr("Script name")
                }

                AppTextField {
                    id: scriptDescription

                    Layout.fillWidth: true
                    compact: true
                    placeholderText: qsTr("Description (optional)")
                    accessibleName: qsTr("Script description")
                }

                AppComboBox {
                    id: scriptShell

                    Layout.fillWidth: true
                    model: ["any", "posix", "powershell"]
                    displayTextModel: [qsTr("Any shell"), qsTr("POSIX shell"), qsTr("PowerShell")]
                    accessibleName: qsTr("Script shell scope")
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Variables")
                        color: Theme.textSoft
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        font.weight: Font.DemiBold
                    }

                    ActionButton {
                        text: qsTr("Add variable")
                        enabled: variableModel.count < 32
                        onClicked: variableModel.append({
                            "name": "variable" + (variableModel.count + 1),
                            "label": qsTr("Variable %1").arg(variableModel.count + 1),
                            "type": "text",
                            "defaultValue": "",
                            "choicesText": "",
                            "required": false
                        })
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: variableModel.count === 0
                    text: qsTr("Variables turn ${name} placeholders into reusable, typed inputs.")
                    color: Theme.textSubtle
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                Repeater {
                    model: ListModel {
                        id: variableModel
                    }

                    delegate: Rectangle {
                        id: variableCard

                        required property int index
                        required property string name
                        required property string label
                        required property string type
                        required property string defaultValue
                        required property string choicesText
                        required property bool required

                        Layout.fillWidth: true
                        Layout.preferredHeight: variableCard.type === "choice" ? 146 : 112
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                AppTextField {
                                    Layout.fillWidth: true
                                    compact: true
                                    text: variableCard.name
                                    placeholderText: qsTr("Variable name")
                                    accessibleName: qsTr("Variable name")
                                    onTextEdited: variableModel.setProperty(variableCard.index, "name", text)
                                }

                                AppTextField {
                                    Layout.fillWidth: true
                                    compact: true
                                    text: variableCard.label
                                    placeholderText: qsTr("Label")
                                    accessibleName: qsTr("Variable label")
                                    onTextEdited: variableModel.setProperty(variableCard.index, "label", text)
                                }

                                ToolButton {
                                    id: removeVariableButton
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    hoverEnabled: true
                                    onClicked: variableModel.remove(variableCard.index)
                                    Accessible.name: qsTr("Remove variable")
                                    contentItem: AppIcon {
                                        name: "trash"
                                        color: Theme.danger
                                    }
                                    background: Rectangle {
                                        radius: height / 2
                                        color: removeVariableButton.down ? Theme.controlPressed : removeVariableButton.hovered ? Theme.controlHover : "transparent"
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                AppComboBox {
                                    Layout.preferredWidth: 116
                                    model: ["text", "integer", "boolean", "choice"]
                                    displayTextModel: [qsTr("Text"), qsTr("Integer"), qsTr("Boolean"), qsTr("Choice")]
                                    currentIndex: model.indexOf(variableCard.type)
                                    accessibleName: qsTr("Variable type")
                                    onActivated: index => variableModel.setProperty(variableCard.index, "type", model[index])
                                }

                                AppTextField {
                                    Layout.fillWidth: true
                                    compact: true
                                    text: variableCard.defaultValue
                                    placeholderText: qsTr("Default value")
                                    accessibleName: qsTr("Default variable value")
                                    onTextEdited: variableModel.setProperty(variableCard.index, "defaultValue", text)
                                }

                                AppCheckBox {
                                    text: qsTr("Required")
                                    checked: variableCard.required
                                    onToggled: variableModel.setProperty(variableCard.index, "required", checked)
                                }
                            }

                            AppTextField {
                                Layout.fillWidth: true
                                visible: variableCard.type === "choice"
                                compact: true
                                text: variableCard.choicesText
                                placeholderText: qsTr("Choices separated by commas")
                                accessibleName: qsTr("Variable choices")
                                onTextEdited: variableModel.setProperty(variableCard.index, "choicesText", text)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Steps")
                        color: Theme.textSoft
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                        font.weight: Font.DemiBold
                    }

                    ActionButton {
                        text: qsTr("Add step")
                        enabled: stepModel.count < 32
                        onClicked: stepModel.append({
                            "command": "",
                            "continuation": "immediate",
                            "outputMarker": "",
                            "timeoutMs": "30000"
                        })
                    }
                }

                Repeater {
                    model: ListModel {
                        id: stepModel
                    }

                    delegate: Rectangle {
                        id: stepCard

                        required property int index
                        required property string command
                        required property string continuation
                        required property string outputMarker
                        required property string timeoutMs

                        Layout.fillWidth: true
                        Layout.preferredHeight: stepCard.continuation === "literal-output" ? 196 : 144
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Step %1").arg(stepCard.index + 1)
                                    color: Theme.text
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                    font.weight: Font.DemiBold
                                }

                                ToolButton {
                                    id: removeStepButton
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    enabled: stepModel.count > 1
                                    hoverEnabled: true
                                    onClicked: stepModel.remove(stepCard.index)
                                    Accessible.name: qsTr("Remove script step")
                                    contentItem: AppIcon {
                                        name: "trash"
                                        color: removeStepButton.enabled ? Theme.danger : Theme.textSubtle
                                    }
                                    background: Rectangle {
                                        radius: height / 2
                                        color: removeStepButton.down ? Theme.controlPressed : removeStepButton.hovered ? Theme.controlHover : "transparent"
                                    }
                                }
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 68
                                clip: true

                                TextArea {
                                    text: stepCard.command
                                    placeholderText: qsTr("Command or multiline shell text")
                                    color: Theme.text
                                    placeholderTextColor: Theme.textMuted
                                    selectionColor: Theme.accent
                                    selectedTextColor: Theme.accentText
                                    wrapMode: TextEdit.NoWrap
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textBody
                                    Accessible.name: qsTr("Script step command")
                                    onTextChanged: stepModel.setProperty(stepCard.index, "command", text)
                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        color: Theme.fieldBackground
                                        border.color: parent.activeFocus ? Theme.focus : Theme.border
                                    }
                                }
                            }

                            AppComboBox {
                                Layout.fillWidth: true
                                model: ["immediate", "literal-output"]
                                displayTextModel: [qsTr("Continue immediately"), qsTr("Wait for literal output")]
                                currentIndex: model.indexOf(stepCard.continuation)
                                accessibleName: qsTr("Step continuation")
                                onActivated: index => stepModel.setProperty(stepCard.index, "continuation", model[index])
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: stepCard.continuation === "literal-output"
                                spacing: 6

                                AppTextField {
                                    Layout.fillWidth: true
                                    compact: true
                                    text: stepCard.outputMarker
                                    placeholderText: qsTr("Literal output marker")
                                    accessibleName: qsTr("Output marker")
                                    onTextEdited: stepModel.setProperty(stepCard.index, "outputMarker", text)
                                }

                                AppTextField {
                                    Layout.preferredWidth: 92
                                    compact: true
                                    text: stepCard.timeoutMs
                                    placeholderText: qsTr("Timeout ms")
                                    accessibleName: qsTr("Output timeout in milliseconds")
                                    validator: IntValidator {
                                        bottom: 1000
                                        top: 300000
                                    }
                                    onTextEdited: stepModel.setProperty(stepCard.index, "timeoutMs", text)
                                }
                            }
                        }
                    }
                }

                StatusMessage {
                    Layout.fillWidth: true
                    kind: "error"
                    text: root.controller.quickCommandOperationError
                }
            }
        }
    }
}
