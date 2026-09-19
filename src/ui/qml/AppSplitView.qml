pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

SplitView {
    id: control
    required property Item leadingPane
    required property Item trailingPane
    property real ratio: 0.5
    // The divider paints as a hairline; the pointer target stays wide through
    // the handle's containmentMask so hover, press and cursor lookup all hit it.
    readonly property int handleThickness: 1
    readonly property int handleHitThickness: 7
    signal ratioEdited(real value)
    readonly property real availableSpan: Math.max(0, (orientation === Qt.Horizontal ? width : height) - handleThickness)
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
        implicitWidth: control.handleThickness
        implicitHeight: control.handleThickness
        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.border
        containmentMask: hitArea
        Item {
            id: hitArea
            readonly property real overhang: (control.handleHitThickness - control.handleThickness) / 2
            x: control.orientation === Qt.Horizontal ? -overhang : 0
            y: control.orientation === Qt.Horizontal ? 0 : -overhang
            width: control.orientation === Qt.Horizontal ? control.handleHitThickness : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : control.handleHitThickness
        }
        SplitHandleObserver {
            anchors.fill: hitArea
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
