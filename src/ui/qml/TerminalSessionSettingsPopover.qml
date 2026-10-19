pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popover
    objectName: "terminalSessionSettingsPopover"

    required property var controller
    property var terminalTab: null
    property var anchorItem: null

    function placeAtAnchor() {
        const item = anchorItem;
        const overlay = Overlay.overlay;
        if (!item || !overlay)
            return;
        const point = item.mapToItem(overlay, item.width - width, item.height + 6);
        const targetX = Math.max(8, Math.min(point.x, overlay.width - width - 8));
        const targetY = Math.max(8, Math.min(point.y, overlay.height - height - 8));
        const localPoint = overlay.mapToItem(item, targetX, targetY);
        x = localPoint.x;
        y = localPoint.y;
    }

    function openFor(item) {
        if (terminalTab === null)
            return;
        fontField.text = terminalTab.sessionFontFamily.length > 0 ? terminalTab.sessionFontFamily : controller.terminalFontFamily;
        sizeBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionFontSize : controller.terminalFontSize) * 100);
        weightBox.currentIndex = (terminalTab.sessionFontSize > 0 ? terminalTab.sessionFontWeight : controller.terminalFontWeight) / 100 - 1;
        lineBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionLineHeight : controller.terminalLineHeight) * 100);
        widthBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionCellWidth : controller.terminalCellWidth) * 100);
        ligatureSwitch.checked = terminalTab.sessionFontSize > 0 ? terminalTab.sessionLigatures : controller.terminalLigatures;
        opacitySlider.value = terminalTab.sessionBackgroundOpacity >= 0 ? terminalTab.sessionBackgroundOpacity : controller.terminalBackgroundOpacity;
        const cursor = terminalTab.sessionCursor.length > 0 ? terminalTab.sessionCursor : controller.cursorPreference;
        cursorBox.currentIndex = Math.max(0, cursorBox.model.indexOf(cursor));
        foregroundField.text = terminalTab.sessionForeground.length > 0 ? terminalTab.sessionForeground : "#F8FAFC";
        backgroundField.text = terminalTab.sessionBackground.length > 0 ? terminalTab.sessionBackground : "#0B1017";
        anchorItem = item;
        parent = item;
        placeAtAnchor();
        open();
        Qt.callLater(placeAtAnchor);
        fontField.forceActiveFocus();
    }

    width: 390
    height: Math.min(contentColumn.implicitHeight + 28, Overlay.overlay ? Overlay.overlay.height - 32 : 760)
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    onHeightChanged: {
        if (visible)
            Qt.callLater(placeAtAnchor);
    }

    enter: MotionEnter {}
    exit: MotionExit {}

    background: AppSurface {
        elevation: 2
    }

    contentItem: ScrollView {
        id: sessionScroll
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            id: contentColumn
            width: sessionScroll.availableWidth
            spacing: 10

            Text {
                text: qsTr("Session terminal settings")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
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
                    text: qsTr("Font size (pt)")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
                Item {
                    Layout.fillWidth: true
                }
                AppDecimalSpinBox {
                    id: sizeBox
                    objectName: "sessionFontSize"
                    from: 600
                    to: 4800
                }
            }
            Text {
                text: qsTr("Font weight")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
            AppComboBox {
                id: weightBox
                objectName: "sessionFontWeight"
                Layout.fillWidth: true
                model: [qsTr("Thin"), qsTr("Extra light"), qsTr("Light"), qsTr("Normal"), qsTr("Medium"), qsTr("Semi bold"), qsTr("Bold"), qsTr("Extra bold"), qsTr("Black")]
                accessibleName: qsTr("Font weight")
            }
            RowLayout {
                Text {
                    text: qsTr("Line height (×)")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
                Item {
                    Layout.fillWidth: true
                }
                AppDecimalSpinBox {
                    id: lineBox
                    from: 100
                    to: 300
                    stepSize: 5
                    accessibleName: qsTr("Line height (×)")
                }
            }
            RowLayout {
                Text {
                    text: qsTr("Cell width (×)")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
                Item {
                    Layout.fillWidth: true
                }
                AppDecimalSpinBox {
                    id: widthBox
                    from: 50
                    to: 300
                    stepSize: 5
                    accessibleName: qsTr("Cell width (×)")
                }
            }
            TerminalFontSample {
                Layout.fillWidth: true
                fontFamily: fontField.text
                fontPointSize: sizeBox.realValue
                fontWeight: (weightBox.currentIndex + 1) * 100
                lineHeightScale: lineBox.realValue
                cellWidthScale: widthBox.realValue
                ligaturesEnabled: ligatureSwitch.checked
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
                        if (popover.controller.setActiveTerminalAppearance(fontField.text, sizeBox.realValue, ligatureSwitch.checked, opacitySlider.value, cursorBox.currentText, foregroundField.text, backgroundField.text, (weightBox.currentIndex + 1) * 100, lineBox.realValue, widthBox.realValue))
                            popover.close();
                    }
                }
            }
        }
    }
}
