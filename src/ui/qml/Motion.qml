pragma Singleton

import QtQuick

// Motion by role, not by speed (UI V2 chapter 3). Every animation in the
// application reads one of these roles; the effects tier and the Windows
// animation preference scale all of them in one place.
//   feedback   hover/press colour, opacity, indicator moves
//   enter/exit popups, menus, tool tips, toasts, dialogs
//   relocate   layout shifts: widths, margins, drawer slides, reordering
//   page       page and settings-category reveals
//   emphasis   pulses and continuous attention cues
QtObject {
    readonly property bool enabled: Theme.motionEnabled
    readonly property bool reduced: Theme.reducedEffects
    readonly property real scale: !enabled ? 0 : reduced ? 0.6 : 1

    readonly property int feedback: Math.round(120 * scale)
    readonly property int enter: Math.round(200 * scale)
    readonly property int exit: Math.round(120 * scale)
    readonly property int relocate: Math.round(200 * scale)
    readonly property int page: Math.round(220 * scale)
    readonly property int emphasis: Math.round(360 * scale)

    // Entering surfaces travel this far and grow from this scale; reduced
    // motion keeps the fade but drops the travel.
    readonly property int distance: !enabled ? 0 : reduced ? 4 : 8
    readonly property real revealScale: enabled && !reduced ? 0.97 : 1.0

    readonly property int feedbackEasing: Easing.OutCubic
    readonly property int enterEasing: Easing.OutQuint
    readonly property int exitEasing: Easing.InCubic
    readonly property int relocateEasing: Easing.OutCubic
    readonly property int emphasisEasing: Easing.InOutSine
}
