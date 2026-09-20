pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

SectionCard {
    id: settings
    property alias closeToTray: closeToTraySwitch.checked
    property alias closePaneOnSessionEnd: closePaneOnSessionEndSwitch.checked
    property alias preserveTerminalSessions: preserveTerminalSessionsSwitch.checked
    property alias reopenLocalSessions: reopenLocalSessionsSwitch.checked
    property alias reconnectRemoteSessions: reconnectRemoteSessionsSwitch.checked
    property alias performanceMode: performanceModeSwitch.checked
    property alias singleInstance: singleInstanceSwitch.checked
    property alias tabDoubleClickIndex: tabDoubleClickBox.currentIndex
    property alias tabCloseButtonIndex: tabCloseButtonBox.currentIndex
    signal performanceModeEdited(bool enabled)
    objectName: "settingsWindowBehaviorCard"
    Layout.fillWidth: true
    heading: qsTr("Window behavior")
    compact: true

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingControl

        AppSwitch {
            id: closeToTraySwitch

            objectName: "settingsCloseToTraySwitch"
            Layout.fillWidth: true
            text: qsTr("Keep ztermy running in the notification area when the window is closed")
            accessibleName: text
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("The tray menu can show or hide the window and exit ztermy completely.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }

        AppSwitch {
            id: closePaneOnSessionEndSwitch

            objectName: "settingsClosePaneOnSessionEndSwitch"
            Layout.fillWidth: true
            text: qsTr("Close a pane automatically when its shell exits or SSH session disconnects")
            accessibleName: text
        }

        AppSwitch {
            id: preserveTerminalSessionsSwitch

            objectName: "settingsPreserveTerminalSessionsSwitch"
            Layout.fillWidth: true
            text: qsTr("Restore terminal tabs and pane layouts on the next launch")
            accessibleName: text
        }

        AppSwitch {
            id: reopenLocalSessionsSwitch

            objectName: "settingsReopenLocalSessionsSwitch"
            Layout.fillWidth: true
            leftPadding: 24
            enabled: preserveTerminalSessionsSwitch.checked
            text: qsTr("Open restored local terminals automatically")
            accessibleName: text
        }

        AppSwitch {
            id: reconnectRemoteSessionsSwitch

            objectName: "settingsReconnectRemoteSessionsSwitch"
            Layout.fillWidth: true
            leftPadding: 24
            enabled: preserveTerminalSessionsSwitch.checked
            text: qsTr("Reconnect restored SSH sessions automatically")
            accessibleName: text
        }

        AppSwitch {
            id: performanceModeSwitch

            objectName: "settingsPerformanceModeSwitch"
            Layout.fillWidth: true
            text: qsTr("Prioritize performance on software-rendered or low-power machines")
            accessibleName: text
            onToggled: settings.performanceModeEdited(checked)
        }

        AppSwitch {
            id: singleInstanceSwitch
            Layout.fillWidth: true
            text: qsTr("Reuse the running instance for the same data directory")
            accessibleName: text
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Portable and installed copies with separate data directories remain independent. Applies on next launch.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        Text {
            text: qsTr("Double-click a terminal tab")
            color: Theme.text
        }
        AppComboBox {
            id: tabDoubleClickBox
            Layout.fillWidth: true
            model: [qsTr("Rename"), qsTr("Close tab"), qsTr("No action")]
        }
        Text {
            text: qsTr("Tab close button")
            color: Theme.text
        }
        AppComboBox {
            id: tabCloseButtonBox
            Layout.fillWidth: true
            model: [qsTr("Always visible"), qsTr("Show on hover or keyboard focus"), qsTr("Hidden (use the tab menu)")]
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Uses a truly opaque window and disables material, shadows and motion. Your selected visual-effects preference is kept and restored when this mode is turned off. Restart required.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }
    }
}
