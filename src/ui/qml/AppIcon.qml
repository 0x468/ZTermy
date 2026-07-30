pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

Item {
    id: icon

    required property string name
    property color color: Theme.text
    property real strokeWidth: 1.5
    readonly property string pathData: {
        switch (name) {
        case "close":
            return "M 4 4 L 12 12 M 12 4 L 4 12";
        case "chevron-up":
            return "M 3.5 10.5 L 8 6 L 12.5 10.5";
        case "chevron-down":
            return "M 3.5 5.5 L 8 10 L 12.5 5.5";
        case "hosts":
            return "M 2.5 3.5 L 13.5 3.5 L 13.5 11.5 L 2.5 11.5 Z M 5 14 L 11 14 M 8 11.5 L 8 14";
        case "plus":
            return "M 8 3 L 8 13 M 3 8 L 13 8";
        case "settings":
            return "M 8 5.25 A 2.75 2.75 0 1 0 8 10.75 A 2.75 2.75 0 1 0 8 5.25 M 8 1.75 L 8 3.25 M 8 12.75 L 8 14.25 M 1.75 8 L 3.25 8 M 12.75 8 L 14.25 8 M 3.58 3.58 L 4.64 4.64 M 11.36 11.36 L 12.42 12.42 M 12.42 3.58 L 11.36 4.64 M 4.64 11.36 L 3.58 12.42";
        case "appearance":
            return "M 8 4.5 A 3.5 3.5 0 1 0 8 11.5 A 3.5 3.5 0 1 0 8 4.5 M 8 1.5 L 8 3 M 8 13 L 8 14.5 M 1.5 8 L 3 8 M 13 8 L 14.5 8 M 3.4 3.4 L 4.45 4.45 M 11.55 11.55 L 12.6 12.6 M 12.6 3.4 L 11.55 4.45 M 4.45 11.55 L 3.4 12.6";
        case "terminal":
            return "M 2.5 3.5 L 13.5 3.5 L 13.5 12.5 L 2.5 12.5 Z M 4.5 6 L 7 8 L 4.5 10 M 8 10 L 11 10";
        case "check":
            return "M 3.5 8 L 6.5 11 L 12.5 4.5";
        default:
            return "";
        }
    }

    implicitWidth: 16
    implicitHeight: 16

    Shape {
        anchors.centerIn: parent
        width: 16
        height: 16
        scale: Math.min(icon.width / width, icon.height / height)
        visible: icon.pathData.length > 0

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: icon.pathData
            }
        }
    }
}
