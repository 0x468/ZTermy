import QtQuick

// One entry of the settings navigation rail.
Rectangle {
    id: control

    required property string title
    required property string iconName
    property string category: ""
    property bool selected: false
    property string actionObjectName: ""

    signal activated

    function focusAction() {
        action.forceActiveFocus();
    }

    implicitHeight: 36
    radius: Theme.radiusControl
    color: selected ? Theme.controlBackground : (action.hovered || action.visualFocus ? Theme.controlHover : "transparent")
    border.color: action.visualFocus ? Theme.focus : "transparent"
    border.width: action.visualFocus ? 1 : 0

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: control.selected ? 18 : 0
        radius: width / 2
        color: Theme.accent

        Behavior on height {
            MotionFeedback {}
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        AppIcon {
            width: 16
            height: 16
            name: control.iconName
            color: control.selected ? Theme.text : Theme.textMuted
        }

        Text {
            text: control.title
            color: control.selected ? Theme.text : Theme.textSoft
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
        accessibleName: qsTr("%1 settings").arg(control.title)
        onActivated: control.activated()
    }
}
