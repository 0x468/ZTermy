pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var controller
    required property var activeTab
    property var script: null
    property int valuesRevision: 0
    property string targetSessionId: ""
    readonly property var runningTargets: controller.terminalTabs.filter(tab => tab.running)
    readonly property var targetTab: {
        for (const tab of controller.terminalTabs) {
            if (tab.sessionId === targetSessionId) {
                return tab;
            }
        }
        return null;
    }
    readonly property bool executionActive: targetTab && (targetTab.scriptExecutionState === "running" || targetTab.scriptExecutionState === "waiting-output")
    readonly property var preview: {
        root.valuesRevision;
        return root.script ? root.controller.renderScript(root.script.id, root.variableValues()) : {
            "ok": false,
            "error": "not-found",
            "steps": []
        };
    }

    signal closed

    function begin(scriptValue) {
        script = scriptValue;
        targetSessionId = activeTab && activeTab.running ? activeTab.sessionId : (runningTargets.length > 0 ? runningTargets[0].sessionId : "");
        runVariableModel.clear();
        for (const variable of scriptValue.variables || []) {
            runVariableModel.append({
                "name": variable.name,
                "label": variable.label,
                "type": variable.type,
                "value": variable.defaultValue || "",
                "choices": variable.choices || [],
                "required": variable.required
            });
        }
        valuesRevision++;
    }

    function variableValues() {
        const result = {};
        for (let index = 0; index < runVariableModel.count; ++index) {
            const variable = runVariableModel.get(index);
            result[variable.name] = variable.value;
        }
        return result;
    }

    function setVariableValue(index, value) {
        runVariableModel.setProperty(index, "value", value);
        valuesRevision++;
    }

    function targetLabel(tab) {
        if (!tab) {
            return qsTr("Unavailable terminal");
        }
        const title = tab.title || qsTr("Terminal");
        const identity = tab.identity || tab.sessionId;
        return title + " · " + identity;
    }

    function targetIndex() {
        for (let index = 0; index < runningTargets.length; ++index) {
            if (runningTargets[index].sessionId === targetSessionId) {
                return index;
            }
        }
        return -1;
    }

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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    text: root.script ? root.script.name : ""
                    color: Theme.text
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.script ? root.script.description : ""
                    color: Theme.textSubtle
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }
            }

            ActionButton {
                text: root.executionActive ? qsTr("Running") : qsTr("Run")
                variant: "primary"
                enabled: root.preview.ok && root.targetTab && root.targetTab.running && !root.executionActive
                onClicked: root.controller.runScript(root.script.id, root.variableValues(), root.targetSessionId)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: qsTr("Target terminal")
                color: Theme.textSoft
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
                font.weight: Font.DemiBold
            }

            AppComboBox {
                Layout.fillWidth: true
                model: root.runningTargets.map(tab => tab.sessionId)
                displayTextModel: root.runningTargets.map(tab => root.targetLabel(tab))
                currentIndex: root.targetIndex()
                accessibleName: qsTr("Script target terminal")
                enabled: !root.executionActive && root.runningTargets.length > 0
                onActivated: index => root.targetSessionId = root.runningTargets[index].sessionId
            }

            Text {
                Layout.fillWidth: true
                text: root.targetTab ? qsTr("This target remains fixed while the script runs.") : qsTr("Open a running terminal before executing the script.")
                color: Theme.textSubtle
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: executionStatus.visible ? 44 : 0
            visible: root.targetTab && root.targetTab.scriptExecutionState !== "idle"
            radius: Theme.radiusControl
            color: Theme.selectedBackground
            border.color: Theme.border

            RowLayout {
                id: executionStatus
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: root.targetTab && root.targetTab.scriptExecutionState === "timed-out" ? Theme.danger : root.executionActive ? Theme.accent : Theme.successText
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!root.targetTab) {
                            return "";
                        }
                        if (root.targetTab.scriptExecutionState === "waiting-output") {
                            return qsTr("Waiting for output · %1/%2 steps sent").arg(root.targetTab.scriptExecutionDispatchedSteps).arg(root.targetTab.scriptExecutionTotalSteps);
                        }
                        if (root.targetTab.scriptExecutionState === "running") {
                            return qsTr("Running · %1/%2 steps sent").arg(root.targetTab.scriptExecutionDispatchedSteps).arg(root.targetTab.scriptExecutionTotalSteps);
                        }
                        if (root.targetTab.scriptExecutionState === "completed") {
                            return qsTr("Script completed");
                        }
                        if (root.targetTab.scriptExecutionState === "cancelled") {
                            return qsTr("Script cancelled");
                        }
                        return qsTr("Script timed out while waiting for output");
                    }
                    color: Theme.textSoft
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                ActionButton {
                    text: qsTr("Cancel")
                    visible: root.executionActive
                    onClicked: root.controller.cancelScript(root.targetSessionId)
                }
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
                    visible: runVariableModel.count > 0
                    text: qsTr("Inputs")
                    color: Theme.textSoft
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: ListModel {
                        id: runVariableModel
                    }

                    delegate: ColumnLayout {
                        id: variableDelegate

                        required property int index
                        required property string name
                        required property string label
                        required property string type
                        required property string value
                        required property var choices
                        required property bool required

                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: variableDelegate.required ? qsTr("%1 · required").arg(variableDelegate.label) : variableDelegate.label
                            color: Theme.textSoft
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }

                        AppComboBox {
                            Layout.fillWidth: true
                            visible: variableDelegate.type === "choice"
                            model: variableDelegate.choices
                            accessibleName: variableDelegate.label
                            currentIndex: Math.max(0, model.indexOf(variableDelegate.value))
                            onActivated: index => root.setVariableValue(variableDelegate.index, model[index])
                        }

                        AppCheckBox {
                            visible: variableDelegate.type === "boolean"
                            text: variableDelegate.label
                            checked: variableDelegate.value === "true"
                            onToggled: root.setVariableValue(variableDelegate.index, checked ? "true" : "false")
                        }

                        AppTextField {
                            Layout.fillWidth: true
                            visible: variableDelegate.type !== "choice" && variableDelegate.type !== "boolean"
                            compact: true
                            text: variableDelegate.value
                            placeholderText: variableDelegate.type === "integer" ? qsTr("Integer value") : qsTr("Value")
                            accessibleName: variableDelegate.label
                            validator: variableDelegate.type === "integer" ? integerValidator : null
                            onTextEdited: root.setVariableValue(variableDelegate.index, text)
                        }

                        IntValidator {
                            id: integerValidator
                        }
                    }
                }

                Text {
                    text: qsTr("Review")
                    color: Theme.textSoft
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.DemiBold
                }

                StatusMessage {
                    Layout.fillWidth: true
                    kind: "error"
                    text: root.preview.ok ? "" : qsTr("Complete the required inputs before running this script.")
                }

                Repeater {
                    model: root.preview.ok ? root.preview.steps : []

                    delegate: Rectangle {
                        id: previewStep

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.preferredHeight: waitText.visible ? 90 : 64
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("Step %1").arg(previewStep.index + 1)
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: previewStep.modelData.continuation === "literal-output" ? qsTr("wait") : qsTr("immediate")
                                    color: Theme.accent
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: previewStep.modelData.command.replace(/\n/g, " ↵ ")
                                color: Theme.text
                                elide: Text.ElideRight
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                id: waitText
                                Layout.fillWidth: true
                                visible: previewStep.modelData.continuation === "literal-output"
                                text: qsTr("Wait for “%1” · %2 s timeout").arg(previewStep.modelData.outputMarker).arg(previewStep.modelData.timeoutMs / 1000)
                                color: Theme.textSubtle
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                }
            }
        }
    }
}
