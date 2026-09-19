pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

SplitView {
    id: control
    required property Item leadingPane
    required property Item trailingPane
    property real ratio: 0.5
    // Marks the divider next to the active terminal pane (Windows Terminal
    // style): the pane itself draws no frame, the shared edge carries the accent.
    property bool emphasized: false
    signal ratioEdited(real value)
    readonly property real availableSpan: Math.max(0, (orientation === Qt.Horizontal ? width : height) - 6)
    readonly property real leadingSize: leadingPane ? (orientation === Qt.Horizontal ? leadingPane.width : leadingPane.height) : 0
    property real dragMinimum: 0
    property real dragMaximum: 0
    property bool resetPending: false

    function paneMinimum(pane) {
        return pane ? (orientation === Qt.Horizontal ? pane.SplitView.minimumWidth : pane.SplitView.minimumHeight) : 0;
    }
    function paneMaximum(pane) {
        return pane ? (orientation === Qt.Horizontal ? pane.SplitView.maximumWidth : pane.SplitView.maximumHeight) : Infinity;
    }
    function lowerLimit() {
        return Math.max(paneMinimum(leadingPane), availableSpan - paneMaximum(trailingPane));
    }
    function upperLimit() {
        return Math.max(lowerLimit(), Math.min(paneMaximum(leadingPane), availableSpan - paneMinimum(trailingPane)));
    }

    ResizeSnap {
        id: snap
        minimum: control.resizing ? control.dragMinimum : control.lowerLimit()
        maximum: control.resizing ? control.dragMaximum : control.upperLimit()
        defaultValue: control.availableSpan / 2
        points: [control.availableSpan / 3, control.availableSpan / 2, control.availableSpan * 2 / 3]
    }
    function setLeadingSize(value) {
        if (!leadingPane)
            return;
        if (orientation === Qt.Horizontal)
            leadingPane.SplitView.preferredWidth = value;
        else
            leadingPane.SplitView.preferredHeight = value;
    }
    function restoreRatio() {
        if (!resizing && availableSpan > 0)
            setLeadingSize(snap.bounded(availableSpan * ratio));
    }
    onAvailableSpanChanged: Qt.callLater(restoreRatio)
    onRatioChanged: Qt.callLater(restoreRatio)
    Component.onCompleted: Qt.callLater(restoreRatio)
    // During a native drag Qt overwrites preferred size. A temporary constraint
    // holds the snap point; restoring the binding preserves the caller's limits.
    Binding {
        target: control.leadingPane ? control.leadingPane.SplitView : null
        property: control.orientation === Qt.Horizontal ? "minimumWidth" : "minimumHeight"
        when: control.resizing && isFinite(snap.heldPoint)
        value: snap.heldPoint
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: control.leadingPane ? control.leadingPane.SplitView : null
        property: control.orientation === Qt.Horizontal ? "maximumWidth" : "maximumHeight"
        when: control.resizing && isFinite(snap.heldPoint)
        value: snap.heldPoint
        restoreMode: Binding.RestoreBindingOrValue
    }
    onResizingChanged: {
        if (resizing) {
            dragMinimum = lowerLimit();
            dragMaximum = upperLimit();
        }
        snap.clear();
        if (!resizing && availableSpan > 0) {
            if (resetPending) {
                resetPending = false;
                Qt.callLater(resetSplit);
            } else {
                ratioEdited(leadingSize / availableSpan);
            }
        }
    }
    function resetSplit() {
        setLeadingSize(snap.reset());
        if (availableSpan > 0)
            ratioEdited(snap.bounded(snap.defaultValue) / availableSpan);
    }
    handle: Rectangle {
        objectName: "splitResizeHandle"
        implicitWidth: 6
        implicitHeight: 6
        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.border
        Rectangle {
            anchors.centerIn: parent
            width: control.orientation === Qt.Horizontal ? 2 : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : 2
            color: Theme.accent
            visible: control.emphasized && !parent.SplitHandle.hovered && !parent.SplitHandle.pressed
        }
        SplitHandleObserver {
            anchors.fill: parent
            property real initialCoordinate: 0
            property real initialSize: 0
            onPointerPressed: position => {
                initialCoordinate = control.orientation === Qt.Horizontal ? position.x : position.y;
                initialSize = control.leadingSize;
            }
            onPointerMoved: position => {
                if (control.resizing) {
                    const coordinate = control.orientation === Qt.Horizontal ? position.x : position.y;
                    snap.resolve(initialSize + coordinate - initialCoordinate);
                }
            }
            onResetRequested: {
                if (control.resizing)
                    control.resetPending = true;
            }
        }
        AppToolTip {
            text: qsTr("Double-click to reset size")
            visible: parent.SplitHandle.hovered && !parent.SplitHandle.pressed
        }
    }
}
