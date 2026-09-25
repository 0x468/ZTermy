pragma ComponentBehavior: Bound

import QtQuick

ActionToast {
    id: toast
    objectName: "terminalNotificationToast"
    required property var controller
    focus: false

    Connections {
        target: toast.controller
        function onTerminalNotificationRequested(notification) {
            toast.present(notification);
        }
    }
}
