pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Terminal (and Settings) tab on the title bar: a TitleTab with session
// status in the icon, a close affordance, reorder drag, and the tab menu.
TitleTab {
    id: control

    property bool running: false
    property bool connecting: false
    property bool canReconnect: false
    property bool canDuplicate: false
    property bool canCloseOthers: false
    property bool canCloseToRight: false
    property bool canMoveLeft: false
    property bool canMoveRight: false
    property string doubleClickAction: "rename"
    property string closeButtonMode: "hover"
    property string closeActionObjectName: ""
    property string workspaceId: ""
    property bool dropCompleted: false
    readonly property bool closeButtonShown: !compact && closeButtonMode !== "hidden"
    readonly property bool tabHovered: hovered || closeAction.hovered
    signal closeRequested
    signal reconnectRequested
    signal duplicateRequested
    signal renameRequested
    signal closeOthersRequested
    signal closeToRightRequested
    signal moveLeftRequested
    signal moveRightRequested
    signal dragMoved(real sceneX)
    signal dragFinished(real sceneX, real sceneY)

    iconSize: 14
    iconColor: iconName === "terminal" && (running || connecting) ? Theme.accent : selected ? Theme.text : Theme.textMuted
    trailingInset: closeButtonShown ? 28 : 0
    accessibleName: qsTr("Activate %1").arg(title)
    doubleClickEnabled: doubleClickAction !== "none"
    toolTip: title
    toolTipEnabled: compact
    onDoubleActivated: {
        if (doubleClickAction === "close")
            closeRequested();
        else if (doubleClickAction === "rename")
            renameRequested();
    }

    SequentialAnimation on iconOpacity {
        running: control.connecting && Motion.enabled
        loops: Animation.Infinite
        onRunningChanged: {
            if (!running)
                control.iconOpacity = 1;
        }

        NumberAnimation {
            to: 0.3
            duration: Motion.emphasis
            easing.type: Motion.emphasisEasing
        }

        NumberAnimation {
            to: 1.0
            duration: Motion.emphasis
            easing.type: Motion.emphasisEasing
        }
    }

    Item {
        id: workspaceDragProxy

        parent: Overlay.overlay
        readonly property point pointerPosition: control.mapToItem(parent, reorderDrag.centroid.position.x, reorderDrag.centroid.position.y)
        x: pointerPosition.x
        y: pointerPosition.y
        width: 1
        height: 1

        DragPreview {
            x: 16
            y: 18
            visible: reorderDrag.active && !control.dropCompleted
            title: control.title
            iconName: control.iconName
        }
    }

    DragHandler {
        id: reorderDrag

        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 8
        enabled: control.workspaceId.length > 0
        onCentroidChanged: {
            if (active)
                control.dragMoved(centroid.scenePosition.x);
        }
        onActiveChanged: {
            if (active) {
                control.dropCompleted = false;
            } else {
                control.dragFinished(centroid.scenePosition.x, centroid.scenePosition.y);
            }
        }
        onCanceled: {
            control.dropCompleted = true;
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: reorderDrag.active
        onActivated: {
            control.dropCompleted = true;
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => {
            tabMenu.x = Math.max(0, Math.min(control.width - tabMenu.width, eventPoint.position.x));
            tabMenu.y = eventPoint.position.y;
            tabMenu.open();
        }
    }

    AppMenu {
        id: tabMenu

        AppMenuItem {
            visible: control.canReconnect
            text: qsTr("Reconnect")
            onTriggered: control.reconnectRequested()
        }
        AppMenuSeparator {
            visible: control.canReconnect
        }
        AppMenuItem {
            text: qsTr("Duplicate tab")
            enabled: control.canDuplicate
            onTriggered: control.duplicateRequested()
        }
        AppMenuItem {
            text: qsTr("Rename tab")
            onTriggered: control.renameRequested()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Move tab left")
            enabled: control.canMoveLeft
            onTriggered: control.moveLeftRequested()
        }
        AppMenuItem {
            text: qsTr("Move tab right")
            enabled: control.canMoveRight
            onTriggered: control.moveRightRequested()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Close tab")
            onTriggered: control.closeRequested()
        }
        AppMenuItem {
            text: qsTr("Close other tabs")
            enabled: control.canCloseOthers
            onTriggered: control.closeOthersRequested()
        }
        AppMenuItem {
            text: qsTr("Close tabs to the right")
            enabled: control.canCloseToRight
            onTriggered: control.closeToRightRequested()
        }
    }

    Rectangle {
        id: closeButton

        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        width: control.closeButtonShown ? 24 : 0
        height: 24
        radius: Theme.radiusCompact
        property real feedbackAmount: closeAction.hovered || closeAction.visualFocus ? 1 : 0
        color: closeAction.pressed ? Theme.captionPressed : Theme.withAlpha(Theme.borderStrong, Theme.borderStrong.a * feedbackAmount)
        visible: control.closeButtonShown
        opacity: control.closeButtonMode === "always" || control.tabHovered || control.visualFocus || closeAction.visualFocus ? 1.0 : 0
        border.color: closeAction.visualFocus ? Theme.focus : "transparent"
        border.width: closeAction.visualFocus ? 1 : 0

        Behavior on feedbackAmount {
            MotionFeedback {}
        }

        Behavior on opacity {
            MotionFeedback {}
        }

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "close"
            color: closeAction.hovered || closeAction.visualFocus ? Theme.text : Theme.textMuted

            Behavior on color {
                MotionColor {}
            }
        }

        KeyboardAction {
            id: closeAction

            objectName: control.closeActionObjectName
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: qsTr("Close %1").arg(control.title)
            onActivated: control.closeRequested()
        }
    }
}
