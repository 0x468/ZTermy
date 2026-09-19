import QtQuick

// Pane header (UI V2 chapter 6): the title strip above a terminal pane. In
// the main window the pane drag capture layer in Main.qml reads paneId,
// paneTitle and dragAreaWidth to move panes between splits and windows; in a
// detached window dragging the header moves that window. The header sits
// inside the pane frame, so the pane's accent border wraps it and the
// hairline at its bottom is the divider above the viewport.
Rectangle {
    id: header

    required property string paneId
    property string paneTitle: ""
    property bool active: false
    property bool detached: false
    property bool running: false
    property bool connecting: false
    property real actionsWidth: 0
    property int cornerRadius: Theme.radiusCompact
    readonly property real dragAreaWidth: width - actionsWidth

    signal activated

    objectName: "terminalPaneHeader-" + paneId
    implicitHeight: 32
    topLeftRadius: cornerRadius
    topRightRadius: cornerRadius
    color: active ? Theme.workspaceRaisedBackground : Theme.workspacePanelBackground

    Behavior on color {
        MotionColor {}
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: header.activated()
    }

    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 10
        enabled: header.detached
        onActiveChanged: {
            if (active && centroid.pressPosition.x < header.dragAreaWidth)
                header.Window.window.startSystemMove();
        }
    }

    SessionStatusDot {
        id: status

        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        running: header.running
        connecting: header.connecting
    }

    Text {
        anchors.left: status.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: header.actionsWidth > 0 ? header.actionsWidth : 10
        anchors.verticalCenter: parent.verticalCenter
        text: header.paneTitle
        color: header.active ? Theme.workspaceText : Theme.workspaceTextMuted
        elide: Text.ElideRight
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
        font.weight: header.active ? Font.DemiBold : Font.Normal

        Behavior on color {
            MotionColor {}
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.workspaceBorder
    }
}
