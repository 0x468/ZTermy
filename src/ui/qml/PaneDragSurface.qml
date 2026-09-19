pragma ComponentBehavior: Bound
import QtQuick

MouseArea {
    id: control
    required property var hostRoot
    required property var coordinator
    required property Item terminalArea

    HoverHandler {
        id: paneHeaderHover
        // Observe the host without placing a permanently visible input layer
        // above its controls. Only the actual header drag claims the pointer.
        parent: control.parent
        blocking: false
        property bool overHeader: false
        onPointChanged: {
            if (control.pressed)
                return;
            const global = control.hostRoot.mapToGlobal(point.position.x, point.position.y);
            const header = control.hostRoot.currentPage === "terminal" && control.hostRoot.paneHeadersVisible ? control.coordinator.viewportAt(control.terminalArea, global, "terminalPaneHeader-") : null;
            overHeader = !!header && header.mapFromGlobal(global.x, global.y).x < header.dragAreaWidth;
        }
        onHoveredChanged: {
            if (!hovered)
                overHeader = false;
        }
    }

    anchors.fill: parent
    z: 80
    visible: pressed || (paneHeaderHover.overHeader && control.hostRoot.currentPage === "terminal" && control.hostRoot.paneHeadersVisible)
    acceptedButtons: Qt.LeftButton
    preventStealing: true
    property point pressPoint: Qt.point(0, 0)
    property point pointerPoint: Qt.point(0, 0)
    property string paneId: ""
    property string paneTitle: ""
    property bool dragging: false
    onPressed: mouse => {
        const global = mapToGlobal(mouse.x, mouse.y);
        const header = control.hostRoot.currentPage === "terminal" ? control.coordinator.viewportAt(control.terminalArea, global, "terminalPaneHeader-") : null;
        if (!header || header.mapFromGlobal(global.x, global.y).x >= header.dragAreaWidth) {
            mouse.accepted = false;
            return;
        }
        paneId = header.paneId;
        paneTitle = header.paneTitle;
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
            if (control.hostRoot.controller.activateTerminalPane(id))
                control.hostRoot.focusTerminalAfterLayout();
        }
        dragging = false;
    }
    function cancelDrag() {
        paneId = "";
        dragging = false;
        control.coordinator.draggedPaneId = "";
        control.coordinator.dropTarget = ({});
    }
    onCanceled: cancelDrag()
}
