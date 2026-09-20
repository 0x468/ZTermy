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
        // above its controls. Only the actual header drag claims the pointer.
        parent: control.parent
        blocking: false
        property bool overDragSource: false
        onPointChanged: {
            if (control.pressed)
                return;
            const global = control.hostRoot.mapToGlobal(point.position.x, point.position.y);
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
    property bool toggleHeadersOnClick: false

    function dragSourceAt(global) {
        if (control.hostRoot.currentPage !== "terminal")
            return null;
        const handle = control.coordinator.viewportAt(control.terminalArea, global, "terminalPaneAction-headers-");
        if (handle)
            return {
                "paneId": handle.dragPaneId,
                "paneTitle": handle.dragPaneTitle,
                "toggleHeaders": true
            };
        if (!control.hostRoot.paneHeadersVisible)
            return null;
        const header = control.coordinator.viewportAt(control.terminalArea, global, "terminalPaneHeader-");
        if (!header || header.mapFromGlobal(global.x, global.y).x >= header.dragAreaWidth)
            return null;
        return {
            "paneId": header.paneId,
            "paneTitle": header.paneTitle,
            "toggleHeaders": false
        };
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
        toggleHeadersOnClick = source.toggleHeaders;
        pressPoint = Qt.point(mouse.x, mouse.y);
        pointerPoint = pressPoint;
        dragging = false;
        control.coordinator.draggedPaneId = paneId;
    }
    onPositionChanged: mouse => {
        if (!pressed || !paneId.length)
            return;
        pointerPoint = Qt.point(mouse.x, mouse.y);
        if (!dragging && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) >= 10)
            dragging = true;
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
            if (toggleHeadersOnClick) {
                control.hostRoot.toggleTerminalPaneHeaders();
            } else if (control.hostRoot.controller.activateTerminalPane(id)) {
                control.hostRoot.focusTerminalAfterLayout();
            }
        }
        dragging = false;
        toggleHeadersOnClick = false;
    }
    function cancelDrag() {
        paneId = "";
        dragging = false;
        toggleHeadersOnClick = false;
        control.coordinator.draggedPaneId = "";
        control.coordinator.dropTarget = ({});
    }
    onCanceled: cancelDrag()
}
