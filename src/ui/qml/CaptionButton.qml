import QtQuick
import QtQuick.Controls

// Window caption command (minimize, maximize/restore, close). The glyphs are
// AppIcon strokes so they follow the icon library and the theme text colour;
// the surface keeps the Win32 hit-testing contract: the maximize button
// leaves its pointer input to the native title bar (Snap Layouts) unless
// nativeMaximizeHandling is off, and surfaceColor exposes the hover fill.
Control {
    id: control

    required property string kind
    required property var chrome
    property bool externallyHovered: false
    property bool externallyPressed: false
    property bool nativeMaximizeHandling: true
    property string accessibleName: ""
    readonly property bool effectiveHovered: externallyHovered || control.hovered || mouseArea.containsMouse
    readonly property bool effectivePressed: externallyPressed || mouseArea.pressed
    readonly property bool closeHighlighted: kind === "close" && (effectiveHovered || visualFocus)
    readonly property color surfaceColor: {
        if (control.closeHighlighted) {
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
    readonly property string glyphName: kind === "minimize" ? "window-minimize" : kind === "close" ? "window-close" : chrome.maximized ? "window-restore" : "window-maximize"
    signal activated

    activeFocusOnTab: true
    background: null
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()

    Rectangle {
        anchors.fill: parent
        color: control.surfaceColor

        Behavior on color {
            MotionColor {}
        }
    }

    Rectangle {
        visible: control.visualFocus
        anchors.fill: parent
        anchors.margins: 3
        color: "transparent"
        border.color: Theme.focus
        border.width: 1
        radius: Theme.radiusSmall
    }

    AppIcon {
        anchors.centerIn: parent
        width: 20
        height: 20
        name: control.glyphName
        color: control.closeHighlighted ? Theme.dangerSurfaceText : Theme.text

        Behavior on color {
            MotionColor {}
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: control.kind !== "maximize" || !control.nativeMaximizeHandling
        hoverEnabled: true
        onClicked: control.activated()
    }

    Keys.onSpacePressed: control.activated()
    Keys.onReturnPressed: control.activated()
}
