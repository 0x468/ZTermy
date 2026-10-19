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
    property bool loading: true
    property string editingTabId: ""

    function scheduleLiveUpdate() {
        if (!loading && visible)
            liveUpdate.restart();
    }

    function applyLive() {
        if (loading || !visible || editingTabId !== controller.activeTerminalTabId)
            return;
        controller.setActiveTerminalAppearance(fontField.text, sizeBox.realValue, ligatureSwitch.checked, opacitySlider.value, cursorBox.currentText, foregroundField.text, backgroundField.text, (weightBox.currentIndex + 1) * 100, lineBox.realValue, widthBox.realValue);
    }

    Timer {
        id: liveUpdate
        interval: 80
        onTriggered: popover.applyLive()
    }

    onAboutToHide: {
        liveUpdate.stop();
        applyLive();
    }

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
        liveUpdate.stop();
        loading = true;
        editingTabId = terminalTab.id;
        fontField.text = terminalTab.sessionFontFamily.length > 0 ? terminalTab.sessionFontFamily : controller.terminalFontFamily;
        sizeBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionFontSize : controller.terminalFontSize) * 100);
        weightBox.currentIndex = (terminalTab.sessionFontSize > 0 ? terminalTab.sessionFontWeight : controller.terminalFontWeight) / 100 - 1;
        lineBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionLineHeight : controller.terminalLineHeight) * 100);
        widthBox.value = Math.round((terminalTab.sessionFontSize > 0 ? terminalTab.sessionCellWidth : controller.terminalCellWidth) * 100);
        ligatureSwitch.checked = terminalTab.sessionFontSize > 0 ? terminalTab.sessionLigatures : controller.terminalLigatures;
        opacitySlider.value = terminalTab.sessionBackgroundOpacity >= 0 ? terminalTab.sessionBackgroundOpacity : controller.terminalBackgroundOpacity;
        const cursor = terminalTab.sessionCursor.length > 0 ? terminalTab.sessionCursor : controller.cursorPreference;
        cursorBox.currentIndex = Math.max(0, cursorBox.model.indexOf(cursor));
        foregroundField.text = terminalTab.sessionForeground;
        backgroundField.text = terminalTab.sessionBackground;
        anchorItem = item;
        parent = item;
        placeAtAnchor();
        open();
        loading = false;
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
                onTextEdited: popover.scheduleLiveUpdate()
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
                    onValueChanged: popover.scheduleLiveUpdate()
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
                onCurrentIndexChanged: popover.scheduleLiveUpdate()
            }
            RowLayout {
                Text {
                    text: qsTr("Line height (natural ×)")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
                Item {
                    Layout.fillWidth: true
                }
                AppDecimalSpinBox {
                    id: lineBox
                    objectName: "sessionLineHeight"
                    from: 100
                    to: 300
                    stepSize: 5
                    accessibleName: qsTr("Line height (natural ×)")
                    onValueChanged: popover.scheduleLiveUpdate()
                }
            }
            RowLayout {
                Text {
                    text: qsTr("Cell width (natural ×)")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
                Item {
                    Layout.fillWidth: true
                }
                AppDecimalSpinBox {
                    id: widthBox
                    objectName: "sessionCellWidth"
                    from: 50
                    to: 300
                    stepSize: 5
                    accessibleName: qsTr("Cell width (natural ×)")
                    onValueChanged: popover.scheduleLiveUpdate()
                }
            }
            AppSwitch {
                id: ligatureSwitch
                text: qsTr("Font ligatures")
                onCheckedChanged: popover.scheduleLiveUpdate()
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
                onValueChanged: popover.scheduleLiveUpdate()
            }
            AppComboBox {
                id: cursorBox
                Layout.fillWidth: true
                model: ["terminal", "block", "bar", "underline"]
                onCurrentIndexChanged: popover.scheduleLiveUpdate()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppTextField {
                    id: foregroundField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Default text color")
                    maximumLength: 9
                    onTextEdited: popover.scheduleLiveUpdate()
                }
                AppTextField {
                    id: backgroundField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Default background color")
                    maximumLength: 9
                    onTextEdited: popover.scheduleLiveUpdate()
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
                        liveUpdate.stop();
                        if (popover.editingTabId === popover.controller.activeTerminalTabId) {
                            popover.controller.resetActiveTerminalAppearance();
                            popover.openFor(popover.anchorItem);
                        }
                    }
                }
                ActionButton {
                    text: qsTr("Done")
                    variant: "primary"
                    onClicked: {
                        popover.applyLive();
                        popover.close();
                    }
                }
            }
        }
    }
}
