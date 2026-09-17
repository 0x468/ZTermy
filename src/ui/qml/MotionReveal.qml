import QtQuick

// Drives a 0..1 "reveal" property that pages bind opacity and a small
// translate to; restart() it after a page or category change.
NumberAnimation {
    from: 0.0
    to: 1.0
    duration: Motion.page
    easing.type: Motion.enterEasing
}
