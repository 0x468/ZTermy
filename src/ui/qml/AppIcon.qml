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
        case "history":
            return "M 3.1 4.4 A 6 6 0 1 1 2.5 9.6 M 2.1 3.1 L 2.8 5.4 L 5.1 4.7 M 8 4.7 L 8 8 L 10.4 9.5";
        case "commands":
            return "M 2.5 3.5 L 13.5 3.5 L 13.5 12.5 L 2.5 12.5 Z M 4.5 6 L 6.8 8 L 4.5 10 M 8 10 L 11 10";
        case "edit":
            return "M 3 11.8 L 3.8 8.9 L 10.7 2 L 14 5.3 L 7.1 12.2 L 4.2 13 Z M 9.6 3.1 L 12.9 6.4";
        case "trash":
            return "M 3 4.5 L 13 4.5 M 6 2.5 L 10 2.5 M 4.5 4.5 L 5.2 13.5 L 10.8 13.5 L 11.5 4.5 M 6.8 7 L 6.8 11 M 9.2 7 L 9.2 11";
        case "search":
            return "M 7 2.5 A 4.5 4.5 0 1 0 7 11.5 A 4.5 4.5 0 1 0 7 2.5 M 10.3 10.3 L 14 14";
        case "copy":
            return "M 5.5 5.5 L 13.5 5.5 L 13.5 13.5 L 5.5 13.5 Z M 2.5 10.5 L 2.5 2.5 L 10.5 2.5";
        case "compose":
            return "M 3 3.5 L 13 3.5 L 13 12.5 L 3 12.5 Z M 5 6 L 11 6 M 5 8.5 L 9 8.5";
        case "play":
            return "M 5 3 L 12.5 8 L 5 13 Z";
        case "save":
            return "M 3 2.5 L 11.5 2.5 L 13.5 4.5 L 13.5 13.5 L 2.5 13.5 L 2.5 2.5 Z M 5 2.5 L 5 6 L 11 6 L 11 2.5 M 5 13.5 L 5 9 L 11 9 L 11 13.5";
        case "refresh":
            return "M 12.5 5 A 5 5 0 1 0 13 10 M 12.5 2.5 L 12.5 5 L 10 5";
        case "swap-horizontal":
            return "M 2.5 5 L 12 5 M 9.5 2.5 L 12 5 L 9.5 7.5 M 13.5 11 L 4 11 M 6.5 8.5 L 4 11 L 6.5 13.5";
        case "more":
            return "M 3 8 A 1 1 0 1 0 3 8.1 M 8 8 A 1 1 0 1 0 8 8.1 M 13 8 A 1 1 0 1 0 13 8.1";
        case "check":
            return "M 3.5 8 L 6.5 11 L 12.5 4.5";
        case "eye":
            return "M 1.5 8 C 3.4 4.7 5.5 3.25 8 3.25 C 10.5 3.25 12.6 4.7 14.5 8 C 12.6 11.3 10.5 12.75 8 12.75 C 5.5 12.75 3.4 11.3 1.5 8 Z M 8 5.75 A 2.25 2.25 0 1 0 8 10.25 A 2.25 2.25 0 1 0 8 5.75";
        case "eye-off":
            return "M 2.25 2.25 L 13.75 13.75 M 5.05 4.15 C 6 3.55 6.95 3.25 8 3.25 C 10.5 3.25 12.6 4.7 14.5 8 C 13.9 9.05 13.2 9.9 12.45 10.55 M 9.95 11.85 C 9.35 12.45 8.7 12.75 8 12.75 C 5.5 12.75 3.4 11.3 1.5 8 C 2.1 6.95 2.8 6.1 3.55 5.45";
        case "lock":
            return "M 3.25 7.25 L 12.75 7.25 L 12.75 14 L 3.25 14 Z M 5.25 7.25 L 5.25 5.25 A 2.75 2.75 0 0 1 10.75 5.25 L 10.75 7.25 M 8 9.75 L 8 11.75";
        case "folder":
            return "M 2 4.5 L 6.3 4.5 L 7.8 6 L 14 6 L 14 13 L 2 13 Z";
        case "new-folder":
            return "M 1.5 5 L 5.8 5 L 7.3 6.5 L 14 6.5 L 14 13.5 L 1.5 13.5 Z M 10.5 2.5 L 10.5 7 M 8.25 4.75 L 12.75 4.75";
        case "file":
            return "M 4 2 L 10 2 L 13 5 L 13 14 L 4 14 Z M 10 2 L 10 5 L 13 5";
        case "upload":
            return "M 8 11 L 8 2.5 M 4.5 6 L 8 2.5 L 11.5 6 M 3 10.5 L 3 13.5 L 13 13.5 L 13 10.5";
        case "download":
            return "M 8 2.5 L 8 11 M 4.5 7.5 L 8 11 L 11.5 7.5 M 3 10.5 L 3 13.5 L 13 13.5 L 13 10.5";
        case "transfer":
            return "M 2.5 5 L 12 5 M 9.5 2.5 L 12 5 L 9.5 7.5 M 13.5 11 L 4 11 M 6.5 8.5 L 4 11 L 6.5 13.5";
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
