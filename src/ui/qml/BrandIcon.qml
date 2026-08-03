pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

Item {
    id: icon

    property color tileColor: Theme.accent
    property color ribbonColor: Theme.accentText
    property color promptColor: Theme.contrastText(ribbonColor)
    property real promptStrokeWidth: 0.64
    property bool promptVisible: true

    Rectangle {
        x: icon.width * 0.05
        y: icon.height * 0.05
        width: icon.width * 0.9
        height: icon.height * 0.9
        radius: Math.min(icon.width, icon.height) * 0.21
        color: icon.tileColor
    }

    Shape {
        anchors.centerIn: parent
        width: 20
        height: 20
        scale: Math.min(icon.width / width, icon.height / height)
        antialiasing: true

        ShapePath {
            strokeWidth: 0
            fillColor: icon.ribbonColor

            PathSvg {
                path: "M5.55 4.85H15.85L14.45 6.75H9.75L6.75 13.25H15.55L14.15 15.15H4.15L7.15 8.65H11.55L12.95 6.75H4.75Z"
            }
        }

        ShapePath {
            strokeColor: icon.promptVisible ? icon.promptColor : "transparent"
            strokeWidth: icon.promptStrokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: "M9.2 9.85 10.5 11 9.2 12.15M11.2 12.15H13.2"
            }
        }
    }
}
