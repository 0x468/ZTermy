import QtQuick
import QtQuick.Controls

Control {
    id: control

    required property string kind
    required property var chrome
    property bool externallyHovered: false
    property bool externallyPressed: false
    property string accessibleName: ""
    readonly property bool effectiveHovered: externallyHovered || control.hovered || mouseArea.containsMouse
    readonly property bool effectivePressed: externallyPressed || mouseArea.pressed
    readonly property color surfaceColor: {
        if (control.kind === "close" && (control.effectiveHovered || control.visualFocus)) {
            return Theme.closeHover;
        }
        if (control.effectivePressed) {
            return Theme.captionPressed;
        }
        if (control.effectiveHovered || control.visualFocus) {
            return Theme.captionHover;
        }
        return "transparent";
    }
    signal activated

    activeFocusOnTab: true
    background: null
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()
    onActiveFocusChanged: icon.requestPaint()
    onVisualFocusChanged: icon.requestPaint()

    Rectangle {
        anchors.fill: parent
        color: control.surfaceColor

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    Rectangle {
        visible: control.visualFocus
        anchors.fill: parent
        anchors.margins: 3
        color: "transparent"
        border.color: Theme.focus
        border.width: 1
        radius: 3
    }

    Canvas {
        id: icon
        anchors.centerIn: parent
        width: 14
        height: 14

        onPaint: {
            const context = getContext("2d");
            context.reset();
            context.strokeStyle = control.kind === "close" && (control.effectiveHovered || control.visualFocus) ? Theme.dangerSurfaceText : Theme.text;
            context.lineWidth = 1;
            context.lineCap = "square";

            if (control.kind === "minimize") {
                context.moveTo(2, 7.5);
                context.lineTo(12, 7.5);
            } else if (control.kind === "maximize") {
                if (control.chrome.maximized) {
                    context.strokeRect(4.5, 2.5, 7, 7);
                    context.moveTo(2.5, 5);
                    context.lineTo(2.5, 11.5);
                    context.lineTo(9, 11.5);
                } else {
                    context.strokeRect(2.5, 2.5, 9, 9);
                }
            } else {
                context.moveTo(3, 3);
                context.lineTo(11, 11);
                context.moveTo(11, 3);
                context.lineTo(3, 11);
            }
            context.stroke();
        }

        Connections {
            target: control.chrome
            function onMaximizedChanged() {
                icon.requestPaint();
            }
        }

        Connections {
            target: Theme
            function onTextChanged() {
                icon.requestPaint();
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: control.kind !== "maximize"
        hoverEnabled: true
        onContainsMouseChanged: icon.requestPaint()
        onClicked: control.activated()
    }

    Keys.onSpacePressed: control.activated()
    Keys.onReturnPressed: control.activated()
}
