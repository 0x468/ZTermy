import QtQuick

Item {
    id: control

    required property string kind
    property bool externallyHovered: false
    property bool externallyPressed: false
    property string accessibleName: ""
    signal activated()

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()

    Rectangle {
        anchors.fill: parent
        color: {
            if (control.kind === "close" && (mouseArea.containsMouse || control.activeFocus)) {
                return "#C42B1C"
            }
            if (control.externallyPressed || mouseArea.pressed) {
                return "#263244"
            }
            if (control.externallyHovered || mouseArea.containsMouse || control.activeFocus) {
                return "#1F2A3A"
            }
            return "transparent"
        }

        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }

    Rectangle {
        visible: control.activeFocus
        anchors.fill: parent
        anchors.margins: 3
        color: "transparent"
        border.color: "#86EFAC"
        border.width: 1
        radius: 3
    }

    Canvas {
        id: icon
        anchors.centerIn: parent
        width: 14
        height: 14

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = "#E2E8F0"
            context.lineWidth = 1
            context.lineCap = "square"

            if (control.kind === "minimize") {
                context.moveTo(2, 7.5)
                context.lineTo(12, 7.5)
            } else if (control.kind === "maximize") {
                if (windowChrome.maximized) {
                    context.strokeRect(4.5, 2.5, 7, 7)
                    context.moveTo(2.5, 5)
                    context.lineTo(2.5, 11.5)
                    context.lineTo(9, 11.5)
                } else {
                    context.strokeRect(2.5, 2.5, 9, 9)
                }
            } else {
                context.moveTo(3, 3)
                context.lineTo(11, 11)
                context.moveTo(11, 3)
                context.lineTo(3, 11)
            }
            context.stroke()
        }

        Connections {
            target: windowChrome
            function onMaximizedChanged() {
                icon.requestPaint()
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: control.kind !== "maximize"
        hoverEnabled: true
        onClicked: control.activated()
    }

    Keys.onSpacePressed: control.activated()
    Keys.onReturnPressed: control.activated()
}
