pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: control

    required property string title
    required property string iconName
    property bool selected: false
    property string actionObjectName: ""
    property string accessibleName: title
    property string toolTip: ""
    signal activated

    implicitWidth: 82
    implicitHeight: Theme.titleBarHeight
    color: selected ? Theme.controlBackground : (action.hovered || action.visualFocus ? Theme.controlHover : "transparent")

    RowLayout {
        anchors.centerIn: parent
        spacing: control.title.length > 0 ? 7 : 0
        AppIcon {
            objectName: control.actionObjectName + "-icon"
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            name: control.iconName
            color: control.selected ? Theme.accent : Theme.textMuted
        }
        Text {
            text: control.title
            visible: text.length > 0
            color: Theme.text
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
        visible: action.hovered && control.toolTip.length > 0
        text: control.toolTip
    }
}
