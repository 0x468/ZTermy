import QtQuick

// Where a dragged tab or pane will land: a thin accent bar between tabs
// ("insert") or a tinted half or whole viewport ("merge" / "swap"). The
// geometry slides while the indicator is shown and stays put while it fades
// out, so a cancelled drag never sweeps the highlight across the window.
Rectangle {
    id: indicator

    property var target: ({})
    readonly property bool shown: !!target && !!target.mode
    readonly property bool insert: shown && target.mode === "insert"

    visible: opacity > 0
    opacity: shown ? 1 : 0
    color: insert ? Theme.accent : Theme.withAlpha(Theme.accent, 0.18)
    border.color: Theme.accent
    border.width: insert ? 0 : 2
    radius: insert ? width / 2 : Theme.radiusSmall
    onTargetChanged: {
        if (!target || !target.mode)
            return;
        x = target.x || 0;
        y = target.y || 0;
        width = target.width || 0;
        height = target.height || 0;
    }

    Behavior on opacity {
        MotionFeedback {}
    }

    Behavior on x {
        enabled: indicator.opacity > 0.99
        MotionRelocate {}
    }

    Behavior on y {
        enabled: indicator.opacity > 0.99
        MotionRelocate {}
    }

    Behavior on width {
        enabled: indicator.opacity > 0.99
        MotionRelocate {}
    }

    Behavior on height {
        enabled: indicator.opacity > 0.99
        MotionRelocate {}
    }
}
