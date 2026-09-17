import QtQuick

// Behavior on color { MotionColor {} }
ColorAnimation {
    duration: Motion.feedback
    easing.type: Motion.feedbackEasing
}
