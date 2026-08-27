import QtQuick
import QtQuick.Controls

Control {
    id: control

    required property string accessibleName
    property bool doubleClickEnabled: false
    signal activated
    signal doubleActivated

    activeFocusOnTab: true
    background: null
    hoverEnabled: true
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
}
