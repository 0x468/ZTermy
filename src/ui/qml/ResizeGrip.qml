pragma ComponentBehavior: Bound
import QtQuick

MouseArea {
    id: grip
    required property real value
    required property real minimum
    required property real maximum
    required property real defaultValue
    property var snapPoints: [defaultValue]
    property bool horizontal: true
    property real direction: 1
    property real startCoordinate: 0
    property real startValue: 0
    signal valueEdited(real value)

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    cursorShape: horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    Accessible.name: qsTr("Resize · double-click to reset")
    ResizeSnap {
        id: snap
        minimum: grip.minimum
        maximum: grip.maximum
        defaultValue: grip.defaultValue
        points: grip.snapPoints
    }
    onPressed: mouse => {
        const global = mapToGlobal(mouse.x, mouse.y);
        startCoordinate = horizontal ? global.x : global.y;
        startValue = value;
        snap.clear();
    }
    onPositionChanged: mouse => {
        if (!pressed)
            return;
        const global = mapToGlobal(mouse.x, mouse.y);
        const coordinate = horizontal ? global.x : global.y;
        valueEdited(snap.resolve(startValue + direction * (coordinate - startCoordinate)));
    }
    onReleased: snap.clear()
    onCanceled: snap.clear()
    onDoubleClicked: valueEdited(snap.reset())
    AppToolTip {
        text: qsTr("Double-click to reset size")
        visible: grip.containsMouse && !grip.pressed
    }
}
