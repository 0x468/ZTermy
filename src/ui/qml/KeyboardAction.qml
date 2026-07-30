import QtQuick

Item {
    id: control

    required property string accessibleName
    readonly property alias hovered: hoverHandler.hovered
    readonly property alias pressed: tapHandler.pressed
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

    HoverHandler {
        id: hoverHandler
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tapHandler

        onTapped: {
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.activated();
        }
    }

    Keys.onSpacePressed: event => control.activateFromKey(event)
    Keys.onReturnPressed: event => control.activateFromKey(event)
    Keys.onEnterPressed: event => control.activateFromKey(event)
}
