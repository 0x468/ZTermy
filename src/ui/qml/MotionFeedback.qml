import QtQuick

// Behavior on opacity/rotation/feedbackAmount { MotionFeedback {} }
NumberAnimation {
    duration: Motion.feedback
    easing.type: Motion.feedbackEasing
}
