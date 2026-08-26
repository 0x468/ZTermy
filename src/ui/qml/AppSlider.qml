pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Slider {
    id: control

    property string accessibleName: ""

    focusPolicy: Qt.StrongFocus
    implicitHeight: 34
    Accessible.name: accessibleName

    background: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.controlBackground

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? Theme.accent : Theme.textSubtle
        }
    }

    handle: Rectangle {
        x: control.leftPadding + (control.visualPosition * (control.availableWidth - width))
        y: (control.height - height) / 2
        width: 18
        height: 18
        radius: width / 2
        color: control.enabled ? Theme.text : Theme.textSubtle
        border.color: control.visualFocus ? Theme.focus : control.hovered ? Theme.accent : Theme.borderStrong
        border.width: control.visualFocus ? 3 : 2
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
