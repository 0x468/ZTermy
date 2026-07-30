pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property string title
    property bool selected: false
    property bool running: false
    signal activated
    signal closeRequested

    implicitWidth: Math.min(190, Math.max(126, titleText.implicitWidth + 54))
    implicitHeight: 42
    color: control.selected || activateAction.hovered || activateAction.activeFocus ? Theme.controlHover : "transparent"
    border.color: activateAction.activeFocus ? Theme.focus : "transparent"
    border.width: activateAction.activeFocus ? 1 : 0

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: control.running ? Theme.accent : Theme.textSubtle
    }

    Text {
        id: titleText

        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.right: closeButton.left
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        text: control.title
        color: Theme.text
        elide: Text.ElideRight
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
    }

    KeyboardAction {
        id: activateAction

        anchors.left: parent.left
        anchors.right: closeButton.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 2
        accessibleName: "Activate " + control.title
        onActivated: control.activated()
    }

    Rectangle {
        id: closeButton

        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 24
        height: 24
        radius: 5
        color: closeAction.hovered || closeAction.activeFocus ? Theme.borderStrong : "transparent"
        border.color: closeAction.activeFocus ? Theme.focus : "transparent"
        border.width: closeAction.activeFocus ? 1 : 0

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "close"
            color: Theme.textMuted
        }

        KeyboardAction {
            id: closeAction

            anchors.fill: parent
            anchors.margins: 2
            accessibleName: "Close " + control.title
            onActivated: control.closeRequested()
        }
    }
}
