pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property string text
    property string iconName: ""
    property string accessibleName: text
    property string actionObjectName: ""
    property bool selected: false
    property bool compact: false
    signal activated

    function focusAction() {
        action.forceActiveFocus();
    }

    implicitHeight: 34
    radius: Theme.radiusControl
    color: action.pressed ? Theme.controlPressed : control.selected ? Theme.controlBackground : action.feedbackColor
    border.color: action.visualFocus ? Theme.focus : "transparent"
    border.width: action.visualFocus ? 1 : 0

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: control.compact ? 2 : 8
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: 18
        radius: width / 2
        color: control.selected ? Theme.accent : "transparent"
    }

    Row {
        id: contentRow

        x: control.compact ? Math.round((control.width - width) / 2) : 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        AppIcon {
            width: control.iconName.length > 0 ? 16 : 0
            height: 16
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.selected ? Theme.text : Theme.textMuted
        }

        Text {
            visible: !control.compact
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

    AppToolTip {
        visible: control.compact && action.hovered
        text: control.accessibleName
    }
}
