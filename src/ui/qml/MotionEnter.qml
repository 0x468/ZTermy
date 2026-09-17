import QtQuick

// enter: MotionEnter {} on Popup, Menu, ToolTip and Dialog: fade in while
// settling from Motion.revealScale to full size.
Transition {
    ParallelAnimation {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Motion.enter
            easing.type: Motion.enterEasing
        }
        NumberAnimation {
            property: "scale"
            from: Motion.revealScale
            to: 1
            duration: Motion.enter
            easing.type: Motion.enterEasing
        }
    }
}
