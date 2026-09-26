pragma ComponentBehavior: Bound

import QtQuick

ActionToast {
    id: toast
    objectName: "terminalNotificationToast"
    required property var controller
    property string ownerWindowId: "main"
    focus: false

    Connections {
        target: toast.controller
        function onTerminalNotificationRequested(notification) {
            if ((notification.windowId || "main") === toast.ownerWindowId)
                toast.present(notification);
        }
    }
}
