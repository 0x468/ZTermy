import QtQuick

Item {
    id: control

    required property string accessibleName
    readonly property alias hovered: pointerArea.containsMouse
    readonly property alias pressed: pointerArea.pressed
    signal activated

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()

    function activateFromKey(event) {
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
    }

    Keys.onSpacePressed: event => control.activateFromKey(event)
    Keys.onReturnPressed: event => control.activateFromKey(event)
    Keys.onEnterPressed: event => control.activateFromKey(event)
}
