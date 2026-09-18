import QtQuick

// Session state at a glance: accent while connected, pulsing while a
// connection is in progress, subtle when idle or closed. Tabs, the tab
// overflow and pane headers share it.
Rectangle {
    id: dot

    property bool running: false
    property bool connecting: false

    implicitWidth: 6
    implicitHeight: 6
    radius: Math.min(width, height) / 2
    color: running || connecting ? Theme.accent : Theme.textSubtle

    Behavior on color {
        MotionColor {}
    }

    SequentialAnimation on opacity {
        running: dot.connecting && Motion.enabled
        loops: Animation.Infinite
        onRunningChanged: {
            if (!running)
                dot.opacity = 1;
        }

        NumberAnimation {
            to: 0.3
            duration: Motion.emphasis
            easing.type: Motion.emphasisEasing
        }

        NumberAnimation {
            to: 1.0
            duration: Motion.emphasis
            easing.type: Motion.emphasisEasing
        }
    }
}
