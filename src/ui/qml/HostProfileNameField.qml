pragma ComponentBehavior: Bound

import QtQuick

AppTextField {
    id: field
    property string profileIcon: "terminal"
    property bool autoFilled: false
    rightPadding: 42
    onActiveFocusChanged: {
        if (activeFocus && autoFilled)
            selectAll();
    }

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: 42
        enabled: field.autoFilled
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.IBeamCursor
        preventStealing: true
        onPressed: field.forceActiveFocus(Qt.MouseFocusReason)
        onReleased: field.selectAll()
    }

    AppIconButton {
        objectName: "hostProfileIconButton"
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        iconName: field.profileIcon
        label: qsTr("Profile icon")
        onClicked: iconMenu.open()
    }

    AppMenu {
        id: iconMenu
        objectName: "hostProfileIconMenu"
        y: field.height
        x: Math.max(0, field.width - width)
        Repeater {
            model: [
                {
                    name: "terminal",
                    label: qsTr("Terminal")
                },
                {
                    name: "hosts",
                    label: qsTr("Host")
                },
                {
                    name: "network",
                    label: qsTr("Network")
                },
                {
                    name: "folder",
                    label: qsTr("Folder")
                },
                {
                    name: "security",
                    label: qsTr("Security")
                },
                {
                    name: "commands",
                    label: qsTr("Commands")
                }
            ]
            delegate: AppMenuItem {
                required property var modelData
                objectName: "hostProfileIcon-" + modelData.name
                text: modelData.label
                iconName: modelData.name
                checkable: true
                checked: field.profileIcon === modelData.name
                onTriggered: field.profileIcon = modelData.name
            }
        }
    }
}
