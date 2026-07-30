pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property string text
    property string accessibleName: text
    property string actionObjectName: ""
    property bool selected: false
    signal activated()

    implicitHeight: 38
    radius: 7
    color: control.selected || action.hovered ? Theme.raisedBackground : "transparent"
    border.color: action.activeFocus ? Theme.focus : "transparent"
    border.width: action.activeFocus ? 1 : 0

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Rectangle {
            width: 3
            height: 16
            radius: 2
            color: control.selected ? Theme.accent : "transparent"
        }

        Text {
            text: control.text
            color: control.selected ? Theme.text : Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }
    }

    KeyboardAction {
        id: action

        objectName: control.actionObjectName
        anchors.fill: parent
        anchors.margins: 2
        accessibleName: control.accessibleName
        onActivated: control.activated()
    }
}
