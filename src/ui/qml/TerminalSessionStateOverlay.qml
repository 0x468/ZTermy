pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// A reserved strip, not a panel over output. Retained terminals remain
// selectable during reconnect and this strip's exit animation.
Item {
    id: overlay

    required property var controller
    required property var tab
    property bool dismissed: false
    readonly property bool local: tab.kind === "local"
    readonly property bool reconnecting: tab.kind === "ssh" && !!tab.reconnecting
    readonly property bool requested: reconnecting || (!controller.closePaneOnSessionEnd && (local ? !!tab.canReopen : !tab.connecting && (!!tab.canReconnect || !!tab.remoteClosed || !!tab.failed)))
    readonly property string heading: reconnecting ? qsTr("Reconnecting to SSH host") : local ? (tab.localExited ? qsTr("Local terminal ended") : qsTr("Local terminal is not open")) : tab.failed ? qsTr("SSH session unavailable") : tab.remoteClosed ? qsTr("SSH session ended") : qsTr("SSH session is disconnected")
    readonly property string reconnectShortcut: controller.actions.find(action => action.id === "terminal.reconnect")?.shortcut || ""
    signal closeRequested
    signal statusDismissed

    objectName: "terminalSessionStateStrip-" + (tab.sessionId || "")
    height: requested && !dismissed ? 44 : 0
    visible: height > 0
    clip: true
    onRequestedChanged: {
        if (!requested)
            dismissed = false;
    }
    onReconnectingChanged: {
        if (reconnecting)
            dismissed = false;
    }

    Behavior on height {
        NumberAnimation {
            duration: overlay.requested && !overlay.dismissed ? Motion.enter : Motion.exit
            easing.type: overlay.requested && !overlay.dismissed ? Motion.enterEasing : Motion.exitEasing
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
        border.color: Theme.border
    }
    MouseArea {
        objectName: "sessionStateInputShield"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }
    RowLayout {
        enabled: overlay.requested && !overlay.dismissed
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: overlay.heading
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
            elide: Text.ElideRight
            AppToolTip {
                text: overlay.tab.status || overlay.heading
                visible: statusHover.hovered
            }
            HoverHandler {
                id: statusHover
            }
        }
        ActionButton {
            visible: overlay.reconnecting || (overlay.local ? !!overlay.tab.canReopen : !!overlay.tab.canReconnect)
            text: overlay.reconnecting ? qsTr("Cancel reconnect") : overlay.local ? qsTr("Open again") : qsTr("Reconnect")
            accessibleName: text
            onClicked: {
                if (overlay.reconnecting)
                    overlay.controller.cancelTerminalReconnect(overlay.tab.sessionId);
                else if (overlay.local)
                    overlay.controller.reopenLocalTerminalTab(overlay.tab.sessionId);
                else
                    overlay.controller.reconnectTerminalTab(overlay.tab.sessionId);
            }
        }
        ActionButton {
            visible: overlay.width >= 360
            text: qsTr("Close pane")
            onClicked: overlay.closeRequested()
        }
        AppIconButton {
            objectName: "dismissSessionStatus-" + (overlay.tab.sessionId || "")
            iconName: "close"
            label: qsTr("Dismiss session status")
            toolTipText: !overlay.local && overlay.tab.canReconnect && overlay.reconnectShortcut.length > 0 ? qsTr("Dismiss session status. Reconnect with %1 while the terminal is focused.").arg(overlay.reconnectShortcut) : label
            onClicked: {
                overlay.dismissed = true;
                overlay.statusDismissed();
            }
        }
    }
}
