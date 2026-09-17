import QtQuick

// Behavior on width/height/x/margins { MotionRelocate {} }
NumberAnimation {
    duration: Motion.relocate
    easing.type: Motion.relocateEasing
}
