pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: control

    required property string title
    property bool selected: false
    property bool running: false
    property bool connecting: false
    property bool canReconnect: false
    property bool canDuplicate: false
    property bool canCloseOthers: false
    property bool canCloseToRight: false
    property bool canMoveLeft: false
    property bool canMoveRight: false
    property string iconName: ""
    property bool compact: false
    property string doubleClickAction: "rename"
    property string closeButtonMode: "hover"
    property string actionObjectName: ""
    property string closeActionObjectName: ""
    property string workspaceId: ""
    property bool dropCompleted: false
    readonly property bool hovered: activateAction.hovered || closeAction.hovered
    signal activated
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

    implicitWidth: compact ? 38 : 112
    implicitHeight: Theme.titleBarHeight
    property real feedbackAmount: control.selected || control.hovered || activateAction.visualFocus ? 1 : 0
    readonly property color feedbackColor: activateAction.pressed ? Theme.captionPressed : control.selected ? Theme.controlBackground : Theme.captionHover
    color: Theme.withAlpha(feedbackColor, feedbackColor.a * feedbackAmount)
    border.color: activateAction.visualFocus ? Theme.focus : "transparent"
    border.width: activateAction.visualFocus ? 1 : 0
    Behavior on feedbackAmount {
        NumberAnimation {
            duration: Theme.motionFast
        }
    }

    Rectangle {
        id: statusDot

        x: control.compact ? (control.width - width) / 2 : 10
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: height / 2
        visible: control.iconName.length === 0
        color: control.running || control.connecting ? Theme.accent : Theme.textSubtle

        SequentialAnimation on opacity {
            running: control.connecting && Theme.animationsEnabled
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.3
                duration: Theme.motionMedium
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 1.0
                duration: Theme.motionMedium
                easing.type: Easing.InOutSine
            }
        }
    }

    AppIcon {
        x: control.compact ? (control.width - width) / 2 : 10
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        visible: control.iconName.length > 0
        name: control.iconName
        color: control.iconName === "terminal" && (control.running || control.connecting) ? Theme.accent : control.selected ? Theme.text : Theme.textMuted
        opacity: control.connecting ? statusDot.opacity : 1
    }

    Text {
        id: titleText

        anchors.left: parent.left
        anchors.leftMargin: control.iconName.length > 0 ? 30 : 24
        anchors.right: closeButton.left
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        text: control.title
        visible: !control.compact
        color: Theme.text
        elide: Text.ElideRight
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
    }

    KeyboardAction {
        id: activateAction

        objectName: control.actionObjectName
        anchors.left: parent.left
        anchors.right: control.compact ? parent.right : closeButton.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 2
        accessibleName: qsTr("Activate %1").arg(control.title)
        onActivated: control.activated()
        doubleClickEnabled: control.doubleClickAction !== "none"
        onDoubleActivated: {
            if (control.doubleClickAction === "close")
                control.closeRequested();
            else if (control.doubleClickAction === "rename")
                control.renameRequested();
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
        AppSurface {
            x: 16
            y: 18
            width: 220
            height: 32
            elevation: 2
            compact: true
            border.color: Theme.accent
            visible: reorderDrag.active && !control.dropCompleted
            Text {
                anchors.fill: parent
                anchors.margins: 8
                text: control.title
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.uiFont
            }
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
        width: control.closeButtonMode === "hidden" ? 0 : 24
        height: 24
        radius: Theme.radiusCompact
        property real feedbackAmount: closeAction.hovered || closeAction.visualFocus ? 1 : 0
        color: closeAction.pressed ? Theme.captionPressed : Theme.withAlpha(Theme.borderStrong, Theme.borderStrong.a * feedbackAmount)
        visible: !control.compact && control.closeButtonMode !== "hidden"
        opacity: control.closeButtonMode === "always" || control.hovered || activateAction.visualFocus || closeAction.visualFocus ? 1.0 : 0
        border.color: closeAction.visualFocus ? Theme.focus : "transparent"
        border.width: closeAction.visualFocus ? 1 : 0

        Behavior on feedbackAmount {
            NumberAnimation {
                duration: Theme.motionFast
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motionFast
            }
        }

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "close"
            color: Theme.textMuted
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

    AppToolTip {
        visible: control.compact && activateAction.hovered
        text: control.title
    }
}
