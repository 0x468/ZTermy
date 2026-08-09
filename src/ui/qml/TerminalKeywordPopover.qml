pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popover

    required property var controller
    property var terminalTab: null
    property string editingId: ""
    readonly property real maximumRulesHeight: 160

    function openFor(item) {
        const overlay = Overlay.overlay;
        const point = item.mapToItem(overlay, 0, item.height + 6);
        const targetX = Math.max(8, Math.min(point.x, overlay.width - width - 8));
        const targetY = Math.max(8, Math.min(point.y, overlay.height - height - 8));
        const localPoint = overlay.mapToItem(item, targetX, targetY);
        parent = item;
        x = localPoint.x;
        y = localPoint.y;
        open();
        patternField.forceActiveFocus();
    }

    function editRule(rule) {
        editingId = rule.id;
        patternField.text = rule.pattern;
        foregroundField.text = rule.foreground;
        backgroundField.text = rule.background;
        enabledSwitch.checked = rule.enabled;
        caseSwitch.checked = rule.caseSensitive;
        patternField.forceActiveFocus();
        patternField.selectAll();
    }

    function clearEditor() {
        editingId = "";
        patternField.text = "";
        foregroundField.text = "#FFFFFF";
        backgroundField.text = "#D13438";
        enabledSwitch.checked = true;
        caseSwitch.checked = false;
    }

    width: 390
    height: Math.min(540, contentColumn.implicitHeight + 28)
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.floatingBackground
        border.color: Theme.borderStrong
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn

        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: qsTr("Host keyword highlighting")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            AppSwitch {
                id: masterSwitch
                checked: popover.terminalTab !== null && popover.terminalTab.keywordHighlightEnabled
                enabled: popover.terminalTab !== null && popover.terminalTab.kind === "ssh"
                accessibleName: qsTr("Enable host keyword highlighting")
                onToggled: popover.controller.setActiveKeywordHighlightEnabled(checked)
            }

            ActionButton {
                objectName: "keywordHighlightCloseAction"
                Layout.preferredWidth: 30
                implicitWidth: 30
                text: ""
                iconName: "close"
                accessibleName: qsTr("Close host keyword highlighting")
                onClicked: popover.close()
            }
        }

        Text {
            Layout.fillWidth: true
            text: popover.terminalTab !== null && popover.terminalTab.keywordHighlightRules.length > 0 ? qsTr("%1 rule(s); the first matching rule wins.").arg(popover.terminalTab.keywordHighlightRules.length) : qsTr("Add a literal keyword and choose its terminal colors.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
        }

        ScrollView {
            id: rulesScrollView

            objectName: "keywordRulesScrollView"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(popover.maximumRulesHeight, Math.ceil(rulesColumn.implicitHeight) + 4)
            visible: popover.terminalTab !== null && popover.terminalTab.keywordHighlightRules.length > 0
            clip: true
            contentWidth: availableWidth
            contentHeight: rulesColumn.implicitHeight
            rightPadding: rulesScrollBar.visible ? rulesScrollBar.width + 4 : 0

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                id: rulesScrollBar

                objectName: "keywordRulesScrollBar"
                policy: rulesColumn.implicitHeight > rulesScrollView.availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            Column {
                id: rulesColumn

                objectName: "keywordRulesColumn"
                width: rulesScrollView.availableWidth
                spacing: 3

                Repeater {
                    model: popover.terminalTab !== null ? popover.terminalTab.keywordHighlightRules : []

                    delegate: ItemDelegate {
                        id: ruleDelegate

                        required property var modelData
                        width: rulesColumn.width
                        height: 38
                        hoverEnabled: true
                        onClicked: popover.editRule(ruleDelegate.modelData)

                        contentItem: RowLayout {
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                radius: 4
                                color: ruleDelegate.modelData.background
                                border.color: Theme.borderStrong
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("A")
                                    color: ruleDelegate.modelData.foreground
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                    font.weight: Font.Bold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: ruleDelegate.modelData.pattern
                                color: ruleDelegate.modelData.enabled ? Theme.text : Theme.textSubtle
                                elide: Text.ElideRight
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textBody
                            }
                            ActionButton {
                                Layout.preferredWidth: 34
                                implicitWidth: 34
                                text: ""
                                iconName: "trash"
                                accessibleName: qsTr("Delete keyword rule")
                                onClicked: {
                                    popover.controller.deleteActiveKeywordHighlightRule(ruleDelegate.modelData.id);
                                    if (popover.editingId === ruleDelegate.modelData.id)
                                        popover.clearEditor();
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        AppTextField {
            id: patternField
            Layout.fillWidth: true
            placeholderText: qsTr("Keyword")
            maximumLength: 128
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppTextField {
                id: foregroundField
                Layout.fillWidth: true
                text: qsTr("#FFFFFF")
                placeholderText: qsTr("Text color")
                maximumLength: 9
            }
            AppTextField {
                id: backgroundField
                Layout.fillWidth: true
                text: qsTr("#D13438")
                placeholderText: qsTr("Background color")
                maximumLength: 9
            }
        }

        RowLayout {
            Layout.fillWidth: true
            AppSwitch {
                id: enabledSwitch
                text: qsTr("Enabled")
                checked: true
            }
            AppSwitch {
                id: caseSwitch
                text: qsTr("Match case")
            }
            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: qsTr("New")
                onClicked: popover.clearEditor()
            }
            ActionButton {
                text: popover.editingId.length > 0 ? qsTr("Update") : qsTr("Add")
                variant: "primary"
                enabled: patternField.text.length > 0
                onClicked: {
                    if (popover.controller.saveActiveKeywordHighlightRule(popover.editingId, patternField.text, foregroundField.text, backgroundField.text, enabledSwitch.checked, caseSwitch.checked))
                        popover.clearEditor();
                }
            }
        }
    }
}
