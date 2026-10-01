pragma ComponentBehavior: Bound
import QtQuick

MouseArea {
    id: control
    required property var hostRoot
    required property var coordinator
    required property Item terminalArea

    HoverHandler {
        id: paneDragSourceHover
        // Observe the host without placing a permanently visible input layer
        // above its controls. Only the actual handle drag claims the pointer.
        parent: control.parent
        blocking: false
        property bool overDragSource: false
        onPointChanged: {
            if (control.pressed)
                return;
            const global = control.mapToGlobal(point.position.x, point.position.y);
            overDragSource = !!control.dragSourceAt(global);
        }
        onHoveredChanged: {
            if (!hovered)
                overDragSource = false;
        }
    }

    anchors.fill: parent
    z: 80
    visible: pressed || (paneDragSourceHover.overDragSource && control.hostRoot.currentPage === "terminal")
    acceptedButtons: Qt.LeftButton
    preventStealing: true
    property point pressPoint: Qt.point(0, 0)
    property point pointerPoint: Qt.point(0, 0)
    property string paneId: ""
    property string paneTitle: ""
    property bool dragging: false
    NativeDragPreview {
        visible: control.dragging
        globalPosition: control.mapToGlobal(control.pointerPoint.x, control.pointerPoint.y)
        title: control.paneTitle
    }

    function dragSourceAt(global) {
        if (control.hostRoot.currentPage !== "terminal")
            return null;
        const handle = control.coordinator.viewportAt(control.terminalArea, global, "terminalPaneAction-drag-");
        if (handle)
            return {
                "paneId": handle.dragPaneId,
                "paneTitle": handle.dragPaneTitle
            };
        return null;
    }

    onPressed: mouse => {
        const global = mapToGlobal(mouse.x, mouse.y);
        const source = control.dragSourceAt(global);
        if (!source) {
            mouse.accepted = false;
            return;
        }
        paneId = source.paneId;
        paneTitle = source.paneTitle;
        pressPoint = Qt.point(mouse.x, mouse.y);
        pointerPoint = pressPoint;
        dragging = false;
        control.coordinator.draggedPaneId = paneId;
    }
    onPositionChanged: mouse => {
        if (!pressed || !paneId.length)
            return;
        pointerPoint = Qt.point(mouse.x, mouse.y);
        if (!dragging && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) >= 10) {
            dragging = true;
        }
        if (dragging)
            control.coordinator.updateDropTarget(mapToGlobal(mouse.x, mouse.y));
    }
    onReleased: mouse => {
        if (!paneId.length)
            return;
        const id = paneId;
        paneId = "";
        if (dragging) {
            control.coordinator.finishPaneDrop(id, mouse.x < 0 || mouse.y < 0 || mouse.x > width || mouse.y > height);
        } else {
            control.coordinator.draggedPaneId = "";
            if (control.hostRoot.controller.activateTerminalPane(id)) {
                control.hostRoot.focusTerminalAfterLayout();
            }
        }
        dragging = false;
    }
    function cancelDrag() {
        control.coordinator.clearTabPreviews();
        paneId = "";
        dragging = false;
        control.coordinator.draggedPaneId = "";
        control.coordinator.dropTarget = ({});
    }
    onCanceled: cancelDrag()
}
