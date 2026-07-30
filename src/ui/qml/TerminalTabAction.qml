pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property string title
    property bool selected: false
    property bool running: false
    property string iconName: ""
    property string actionObjectName: ""
    property string closeActionObjectName: ""
    property real enterProgress: Theme.animationsEnabled ? 0.0 : 1.0
    signal activated
    signal closeRequested

    implicitWidth: Math.min(184, Math.max(112, titleText.implicitWidth + 54))
    implicitHeight: Theme.titleBarHeight
    opacity: enterProgress
    color: control.selected ? Theme.controlBackground : (activateAction.hovered || activateAction.activeFocus ? Theme.controlHover : "transparent")
    border.color: activateAction.activeFocus ? Theme.focus : "transparent"
    border.width: activateAction.activeFocus ? 1 : 0
    transform: Translate {
        x: -Theme.motionDistanceSmall * (1.0 - control.enterProgress)
    }

    Component.onCompleted: {
        if (Theme.animationsEnabled) {
            enterAnimation.start();
        } else {
            enterProgress = 1.0;
        }
    }

    NumberAnimation {
        id: enterAnimation

        target: control
        property: "enterProgress"
        from: 0.0
        to: 1.0
        duration: Theme.motionSlow
        easing.type: Easing.OutCubic
    }

    Behavior on color {
        ColorAnimation {
            duration: Theme.motionFast
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        visible: control.iconName.length === 0
        color: control.running ? Theme.accent : Theme.textSubtle
    }

    AppIcon {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        visible: control.iconName.length > 0
        name: control.iconName
        color: control.selected ? Theme.text : Theme.textMuted
    }

    Text {
        id: titleText

        anchors.left: parent.left
        anchors.leftMargin: control.iconName.length > 0 ? 30 : 24
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

        objectName: control.actionObjectName
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
        opacity: control.selected || activateAction.hovered || activateAction.activeFocus || closeAction.hovered || closeAction.activeFocus ? 1.0 : 0.45
        border.color: closeAction.activeFocus ? Theme.focus : "transparent"
        border.width: closeAction.activeFocus ? 1 : 0

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motionFast
            }
        }

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "close"
            color: Theme.textMuted
        }

        KeyboardAction {
            id: closeAction

            objectName: control.closeActionObjectName
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: "Close " + control.title
            onActivated: control.closeRequested()
        }
    }
}
