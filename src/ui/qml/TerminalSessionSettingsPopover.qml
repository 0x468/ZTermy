pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popover

    required property var controller
    property var terminalTab: null

    function openFor(item) {
        if (terminalTab === null)
            return;
        fontField.text = terminalTab.sessionFontFamily.length > 0 ? terminalTab.sessionFontFamily : controller.terminalFontFamily;
        sizeBox.value = terminalTab.sessionFontSize > 0 ? terminalTab.sessionFontSize : controller.terminalFontSize;
        ligatureSwitch.checked = terminalTab.sessionFontSize > 0 ? terminalTab.sessionLigatures : controller.terminalLigatures;
        opacitySlider.value = terminalTab.sessionBackgroundOpacity >= 0 ? terminalTab.sessionBackgroundOpacity : controller.terminalBackgroundOpacity;
        const cursor = terminalTab.sessionCursor.length > 0 ? terminalTab.sessionCursor : controller.cursorPreference;
        cursorBox.currentIndex = Math.max(0, cursorBox.model.indexOf(cursor));
        foregroundField.text = terminalTab.sessionForeground.length > 0 ? terminalTab.sessionForeground : "#F8FAFC";
        backgroundField.text = terminalTab.sessionBackground.length > 0 ? terminalTab.sessionBackground : "#0B1017";
        const overlay = Overlay.overlay;
        const point = item.mapToItem(overlay, item.width - width, item.height + 6);
        const targetX = Math.max(8, Math.min(point.x, overlay.width - width - 8));
        const targetY = Math.max(8, Math.min(point.y, overlay.height - height - 8));
        const localPoint = overlay.mapToItem(item, targetX, targetY);
        parent = item;
        x = localPoint.x;
        y = localPoint.y;
        open();
        fontField.forceActiveFocus();
    }

    width: 390
    height: contentColumn.implicitHeight + 28
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

        Text {
            text: qsTr("Session terminal settings")
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textTitle
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Overrides apply only to this tab and are discarded when it closes.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
        }

        AppTextField {
            id: fontField
            Layout.fillWidth: true
            placeholderText: qsTr("Font family")
            maximumLength: 128
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Font size")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
            Item {
                Layout.fillWidth: true
            }
            AppSpinBox {
                id: sizeBox
                from: 8
                to: 32
            }
        }
        AppSwitch {
            id: ligatureSwitch
            text: qsTr("Font ligatures")
        }
        Text {
            text: qsTr("Terminal background opacity: %1%").arg(Math.round(opacitySlider.value * 100))
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }
        AppSlider {
            id: opacitySlider
            Layout.fillWidth: true
            from: 0
            to: 1
            stepSize: 0.01
        }
        AppComboBox {
            id: cursorBox
            Layout.fillWidth: true
            model: ["terminal", "block", "bar", "underline"]
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppTextField {
                id: foregroundField
                Layout.fillWidth: true
                placeholderText: qsTr("Default text color")
                maximumLength: 9
            }
            AppTextField {
                id: backgroundField
                Layout.fillWidth: true
                placeholderText: qsTr("Default background color")
                maximumLength: 9
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: qsTr("Use global defaults")
                onClicked: {
                    popover.controller.resetActiveTerminalAppearance();
                    popover.close();
                }
            }
            ActionButton {
                text: qsTr("Apply")
                variant: "primary"
                onClicked: {
                    if (popover.controller.setActiveTerminalAppearance(fontField.text, sizeBox.value, ligatureSwitch.checked, opacitySlider.value, cursorBox.currentText, foregroundField.text, backgroundField.text))
                        popover.close();
                }
            }
        }
    }
}
