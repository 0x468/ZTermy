pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: overlay

    required property var controller
    required property var tab
    signal closeRequested
    signal browseHostsRequested

    StatePanel {
        anchors.centerIn: parent
        width: Math.max(180, Math.min(440, parent.width - 24))
        visible: overlay.tab.kind === "local" && overlay.tab.localExited
        kind: "disconnected"
        heading: qsTr("Local terminal ended")
        description: overlay.tab.status || ""
        detail: qsTr("The local shell process exited normally. Close this pane or open another terminal.")
        ActionButton {
            text: qsTr("Close pane")
            accessibleName: qsTr("Close ended local terminal pane")
            variant: "primary"
            onClicked: overlay.closeRequested()
        }
    }

    StatePanel {
        anchors.centerIn: parent
        width: Math.max(180, Math.min(440, parent.width - 24))
        visible: overlay.tab.kind === "ssh" && overlay.tab.canReconnect && !overlay.tab.connecting && !overlay.tab.reconnecting && !overlay.tab.remoteClosed && !overlay.tab.failed
        kind: "disconnected"
        heading: qsTr("SSH session is disconnected")
        description: qsTr("Reconnect to continue using this restored terminal tab.")
        detail: qsTr("The tab layout was restored, but SSH connections are not kept alive after ztermy exits.")
        ActionButton {
            text: qsTr("Reconnect")
            accessibleName: qsTr("Reconnect restored SSH terminal pane")
            variant: "primary"
            onClicked: overlay.controller.reconnectTerminalTab(overlay.tab.sessionId)
        }
        ActionButton {
            text: qsTr("Close pane")
            accessibleName: qsTr("Close disconnected SSH terminal pane")
            onClicked: overlay.closeRequested()
        }
    }

    StatePanel {
        anchors.centerIn: parent
        width: Math.max(180, Math.min(440, parent.width - 24))
        visible: overlay.tab.kind === "ssh" && overlay.tab.remoteClosed && !overlay.tab.reconnecting
        kind: "disconnected"
        heading: qsTr("SSH session ended")
        description: overlay.tab.status || ""
        detail: qsTr("The remote host closed the terminal connection. Reconnect is available for saved host profiles.")
        ActionButton {
            visible: !!overlay.tab.canReconnect
            text: qsTr("Reconnect")
            accessibleName: qsTr("Reconnect saved SSH terminal pane")
            variant: "primary"
            onClicked: overlay.controller.reconnectTerminalTab(overlay.tab.sessionId)
        }
        ActionButton {
            text: qsTr("Close pane")
            accessibleName: qsTr("Close ended SSH terminal pane")
            onClicked: overlay.closeRequested()
        }
        ActionButton {
            text: qsTr("Review host")
            accessibleName: qsTr("Return to SSH host profiles")
            onClicked: overlay.browseHostsRequested()
        }
    }

    StatePanel {
        anchors.centerIn: parent
        width: Math.max(180, Math.min(440, parent.width - 24))
        visible: overlay.tab.kind === "ssh" && overlay.tab.failed && !overlay.tab.reconnecting
        kind: "error"
        heading: qsTr("SSH session unavailable")
        description: overlay.tab.status || ""
        detail: qsTr("Review the saved host and authentication settings, or retry the connection.")
        ActionButton {
            visible: !!overlay.tab.canReconnect
            text: qsTr("Reconnect")
            accessibleName: qsTr("Reconnect saved SSH terminal pane")
            variant: "primary"
            onClicked: overlay.controller.reconnectTerminalTab(overlay.tab.sessionId)
        }
        ActionButton {
            text: qsTr("Close pane")
            accessibleName: qsTr("Close failed SSH terminal pane")
            onClicked: overlay.closeRequested()
        }
        ActionButton {
            text: qsTr("Review host")
            accessibleName: qsTr("Return to SSH host profiles")
            onClicked: overlay.browseHostsRequested()
        }
    }
}
