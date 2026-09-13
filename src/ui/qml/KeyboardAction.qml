import QtQuick
import QtQuick.Controls

Control {
    id: control

    required property string accessibleName
    property bool doubleClickEnabled: false
    property bool keyPressed: false
    readonly property bool pressed: pointerArea.pressed || keyPressed
    readonly property color feedbackColor: pressed ? Theme.captionPressed : hovered || pointerArea.containsMouse || visualFocus ? Theme.captionHover : "transparent"
    signal activated
    signal doubleActivated

    activeFocusOnTab: true
    background: null
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()

    function activateFromKey(event) {
        keyPressed = true;
        if (!event.isAutoRepeat) {
            activated();
        }
        event.accepted = true;
    }

    MouseArea {
        id: pointerArea

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.activated();
        }
        onDoubleClicked: {
            if (!control.doubleClickEnabled)
                return;
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.doubleActivated();
        }
    }

    Keys.onSpacePressed: event => control.activateFromKey(event)
    Keys.onReturnPressed: event => control.activateFromKey(event)
    Keys.onEnterPressed: event => control.activateFromKey(event)
    Keys.onReleased: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            control.keyPressed = false;
            event.accepted = true;
        } else
            event.accepted = false;
    }
    onActiveFocusChanged: {
        if (!activeFocus)
            keyPressed = false;
    }
}
