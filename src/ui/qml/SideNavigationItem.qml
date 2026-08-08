pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property string text
    property string iconName: ""
    property string accessibleName: text
    property string actionObjectName: ""
    property bool selected: false
    signal activated

    implicitHeight: 34
    radius: 7
    color: control.selected || action.hovered ? Theme.raisedBackground : "transparent"
    border.color: action.activeFocus ? Theme.focus : "transparent"
    border.width: action.activeFocus ? 1 : 0

    Rectangle {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: 18
        radius: 2
        color: control.selected ? Theme.accent : "transparent"
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        AppIcon {
            width: control.iconName.length > 0 ? 15 : 0
            height: 15
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.selected ? Theme.text : Theme.textMuted
        }

        Text {
            text: control.text
            color: control.selected ? Theme.text : Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
            font.weight: control.selected ? Font.DemiBold : Font.Normal
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
