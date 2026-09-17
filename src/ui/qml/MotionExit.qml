import QtQuick

// exit: MotionExit {} on Popup, Menu, ToolTip and Dialog.
Transition {
    NumberAnimation {
        property: "opacity"
        from: 1
        to: 0
        duration: Motion.exit
        easing.type: Motion.exitEasing
    }
}
