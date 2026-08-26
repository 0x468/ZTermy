pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property string accessibleName: ""

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: 34
    Accessible.name: accessibleName

    contentItem: TextInput {
        z: 2
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textSubtle
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
    }

    up.indicator: Rectangle {
        x: control.width - width
        height: control.height
        implicitWidth: 34
        color: control.up.pressed ? Theme.controlPressed : control.up.hovered ? Theme.controlHover : "transparent"

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "plus"
            color: control.enabled ? Theme.text : Theme.textSubtle
        }
    }

    down.indicator: Rectangle {
        x: 0
        height: control.height
        implicitWidth: 34
        color: control.down.pressed ? Theme.controlPressed : control.down.hovered ? Theme.controlHover : "transparent"

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "minus"
            color: control.enabled ? Theme.text : Theme.textSubtle
        }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 34
        radius: Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.visualFocus ? Theme.focus : control.hovered ? Theme.borderStrong : Theme.border
        border.width: control.visualFocus ? 2 : 1
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
