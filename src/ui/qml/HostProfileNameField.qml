pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppTextField {
    id: field
    property string profileIcon: "terminal"
    property bool autoFilled: false
    readonly property var commonIcons: [
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
        },
        {
            name: "server",
            label: qsTr("Server")
        },
        {
            name: "cloud",
            label: qsTr("Cloud")
        },
        {
            name: "database",
            label: qsTr("Database")
        },
        {
            name: "router",
            label: qsTr("Router")
        },
        {
            name: "device-desktop",
            label: qsTr("Desktop")
        },
        {
            name: "world",
            label: qsTr("Internet")
        }
    ]
    readonly property var systemIcons: [
        {
            name: "brand-ubuntu",
            label: "Ubuntu"
        },
        {
            name: "brand-debian",
            label: "Debian"
        },
        {
            name: "brand-redhat",
            label: "Red Hat"
        },
        {
            name: "brand-windows",
            label: "Windows"
        },
        {
            name: "brand-apple",
            label: "macOS"
        },
        {
            name: "brand-docker",
            label: "Docker"
        },
        {
            name: "brand-github",
            label: "GitHub"
        }
    ]
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
    component IconChoice: AppIconButton {
        id: choice
        required property var modelData
        Layout.preferredWidth: 48
        Layout.preferredHeight: 48
        leftPadding: 0
        rightPadding: 0
        objectName: "hostProfileIcon-" + modelData.name
        label: modelData.label
        iconName: modelData.name
        selected: field.profileIcon === modelData.name
        onClicked: {
            field.profileIcon = modelData.name;
            iconMenu.close();
        }
        contentItem: ColumnLayout {
            spacing: 3
            AppIcon {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                name: choice.iconName
                color: choice.selected ? Theme.accent : Theme.text
            }
            Text {
                Layout.fillWidth: true
                text: choice.label
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
                color: choice.selected ? Theme.accent : Theme.textSoft
            }
        }
    }
    Popup {
        id: iconMenu
        objectName: "hostProfileIconMenu"
        y: field.height
        x: Math.max(0, field.width - width)
        width: 336
        padding: 12
        margins: 8
        focus: true
        onClosed: field.forceActiveFocus(Qt.PopupFocusReason)
        background: AppSurface {
            elevation: 2
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("General")
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
            GridLayout {
                columns: 6
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: field.commonIcons
                    delegate: IconChoice {}
                }
            }
            Text {
                text: qsTr("Systems and platforms")
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
            GridLayout {
                columns: 6
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: field.systemIcons
                    delegate: IconChoice {}
                }
            }
        }
    }
}
