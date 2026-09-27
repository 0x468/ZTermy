import QtQuick
import QtQuick.Window

Window {
    id: preview
    objectName: "terminalNativeDragPreview"
    property point globalPosition: Qt.point(0, 0)
    property string title: ""
    property string iconName: "terminal"
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus | Qt.WindowStaysOnTopHint
    color: "transparent"
    width: 220
    height: 32
    x: globalPosition.x + 16
    y: globalPosition.y + 18
    DragPreview {
        title: preview.title
        iconName: preview.iconName
    }
}
