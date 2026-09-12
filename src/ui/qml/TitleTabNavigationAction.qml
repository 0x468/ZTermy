import QtQuick

Rectangle {
    id: control

    required property string iconName
    required property string accessibleName
    required property string actionObjectName
    required property int direction
    property color iconColor: Theme.text

    signal activated(int direction)

    width: 26
    height: parent ? parent.height : 0
    color: action.hovered || action.visualFocus ? Theme.controlHover : "transparent"

    AppIcon {
        anchors.centerIn: parent
        width: 13
        height: 13
        name: control.iconName
        color: control.iconColor
    }

    KeyboardAction {
        id: action

        objectName: control.actionObjectName
        anchors.fill: parent
        anchors.margins: 2
        accessibleName: control.accessibleName
        onActivated: control.activated(control.direction)
    }

    AppToolTip {
        visible: action.hovered
        text: control.accessibleName
    }
}
