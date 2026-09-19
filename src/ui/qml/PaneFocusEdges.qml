import QtQuick

// Accent lines on the edges a terminal pane shares with a sibling. `edges` is a
// bitmask (1 left, 2 top, 4 right, 8 bottom); the window edge is never marked.
Item {
    id: control

    property int edges: 0
    property bool shown: false
    readonly property int thickness: 2

    anchors.fill: parent
    visible: shown && edges !== 0

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: control.thickness
        color: Theme.accent
        visible: (control.edges & 1) !== 0
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: control.thickness
        color: Theme.accent
        visible: (control.edges & 2) !== 0
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: control.thickness
        color: Theme.accent
        visible: (control.edges & 4) !== 0
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: control.thickness
        color: Theme.accent
        visible: (control.edges & 8) !== 0
    }
}
