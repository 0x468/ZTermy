pragma ComponentBehavior: Bound

import QtQuick

Row {
    id: controls
    required property var hostRoot
    required property var transferPopup
    required property var commandPopup
    readonly property bool compact: hostRoot.width < 700

    objectName: "titleControls"
    anchors.right: parent.right
    anchors.top: parent.top
    height: parent.height

    Rectangle {
        width: controls.hostRoot.titleSecurityActionWidth
        height: controls.height
        visible: controls.hostRoot.portableVaultNeedsAttention
        color: portableVaultStatusAction.feedbackColor
        border.color: portableVaultStatusAction.visualFocus ? Theme.focus : "transparent"
        border.width: portableVaultStatusAction.visualFocus ? 1 : 0

        AppIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            name: "lock"
            color: Theme.dangerText
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.top: parent.top
            anchors.topMargin: 7
            width: 6
            height: 6
            radius: 3
            color: Theme.danger
            border.color: Theme.chromeBackground
            border.width: 1
        }

        KeyboardAction {
            id: portableVaultStatusAction

            objectName: "portableVaultStatusAction"
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: controls.hostRoot.controller.portableVaultInitialized ? qsTr("Portable vault locked; unlock") : qsTr("Portable vault not configured; open Security settings")
            onActivated: controls.hostRoot.requestPortableVaultAccess(portableVaultStatusAction)
        }
    }

    Rectangle {
        id: alwaysOnTopContainer

        objectName: "alwaysOnTopContainer"
        width: controls.hostRoot.titleQuickActionWidth
        height: controls.height
        color: "transparent"

        Row {
            anchors.fill: parent

            Rectangle {
                width: 26
                height: parent.height
                color: alwaysOnTopAction.feedbackColor

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: controls.hostRoot.windowAlwaysOnTopRequested ? "pin-window" : controls.hostRoot.currentTerminalTabPinned ? "pin-tab" : "pin"
                    color: controls.hostRoot.windowAlwaysOnTopRequested || controls.hostRoot.currentTerminalTabPinned ? Theme.accent : controls.hostRoot.mutedColor
                }

                KeyboardAction {
                    id: alwaysOnTopAction

                    objectName: "alwaysOnTopAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    enabled: true
                    doubleClickEnabled: true
                    accessibleName: controls.hostRoot.windowAlwaysOnTopRequested ? qsTr("Turn off window always on top") : controls.hostRoot.currentPage !== "terminal" || controls.hostRoot.activeTerminalTab === null ? qsTr("Keep window always on top") : controls.hostRoot.currentTerminalTabPinned ? qsTr("Unpin current terminal tab") : qsTr("Pin current terminal tab")
                    onActivated: controls.hostRoot.activatePinPrimary()
                    onDoubleActivated: controls.hostRoot.activatePinDouble()
                }

                AppToolTip {
                    visible: alwaysOnTopAction.hovered && !alwaysOnTopMenu.visible
                    text: {
                        if (controls.hostRoot.windowAlwaysOnTopRequested && controls.hostRoot.currentTerminalTabPinned) {
                            return qsTr("Current tab pinned · window always on top\nDouble-click to turn off window pinning");
                        }
                        if (controls.hostRoot.windowAlwaysOnTopRequested) {
                            return qsTr("Window always on top\nClick to turn off · Double-click toggles window pinning");
                        }
                        if (controls.hostRoot.currentTerminalTabPinned) {
                            return qsTr("Current terminal tab pinned\nClick to unpin · Double-click pins the whole window");
                        }
                        if (controls.hostRoot.currentPage !== "terminal" || controls.hostRoot.activeTerminalTab === null) {
                            return qsTr("Click or double-click to keep the whole window always on top");
                        }
                        return qsTr("Click to pin this tab · Double-click to pin the whole window");
                    }
                }
            }

            Rectangle {
                width: 14
                height: parent.height
                color: alwaysOnTopMenuAction.pressed ? Theme.captionPressed : alwaysOnTopMenu.visible ? Theme.captionHover : alwaysOnTopMenuAction.feedbackColor
                border.color: alwaysOnTopMenuAction.visualFocus ? Theme.focus : "transparent"
                border.width: alwaysOnTopMenuAction.visualFocus ? 1 : 0

                AppIcon {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    name: "chevron-down"
                    color: controls.hostRoot.mutedColor
                }

                KeyboardAction {
                    id: alwaysOnTopMenuAction

                    objectName: "alwaysOnTopMenuAction"
                    anchors.fill: parent
                    accessibleName: qsTr("Open pin options")
                    onActivated: alwaysOnTopMenu.open()
                }

                AppToolTip {
                    visible: alwaysOnTopMenuAction.hovered && !alwaysOnTopMenu.visible
                    text: qsTr("Pin options")
                }
            }
        }

        AppMenu {
            id: alwaysOnTopMenu

            objectName: "alwaysOnTopMenu"
            x: alwaysOnTopContainer.width - width
            y: alwaysOnTopContainer.height

            AppMenuItem {
                objectName: "pinCurrentTerminalTabMenuAction"
                text: qsTr("Pin current terminal tab")
                iconName: "pin-tab"
                checkable: true
                checked: controls.hostRoot.currentTerminalTabPinned
                enabled: controls.hostRoot.currentPage === "terminal" && controls.hostRoot.activeTerminalTab !== null
                onTriggered: controls.hostRoot.toggleActiveTerminalPin()
            }

            AppMenuItem {
                objectName: "pinWindowMenuAction"
                text: qsTr("Keep window always on top")
                iconName: "pin-window"
                checkable: true
                checked: controls.hostRoot.windowAlwaysOnTopRequested
                onTriggered: controls.hostRoot.setWindowAlwaysOnTopRequested(!controls.hostRoot.windowAlwaysOnTopRequested)
            }

            AppMenuSeparator {}

            AppMenuItem {
                objectName: "disablePinningMenuAction"
                text: qsTr("Turn off pinning")
                iconName: "close"
                enabled: controls.hostRoot.windowAlwaysOnTopRequested || controls.hostRoot.currentTerminalTabPinned
                onTriggered: controls.hostRoot.clearAlwaysOnTopPreference()
            }
        }
    }

    Rectangle {
        visible: !controls.compact
        width: visible ? controls.hostRoot.titleQuickActionWidth : 0
        height: controls.height
        color: transferCenterAction.feedbackColor
        border.color: transferCenterAction.visualFocus ? Theme.focus : "transparent"
        border.width: transferCenterAction.visualFocus ? 1 : 0

        AppIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            name: "transfer"
            color: controls.transferPopup.visible ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 5
            visible: controls.hostRoot.controller.activeTransferCount > 0
            width: Math.max(12, transferCountText.implicitWidth + 4)
            height: 12
            radius: 6
            color: Theme.accent

            Text {
                id: transferCountText

                anchors.centerIn: parent
                text: controls.hostRoot.controller.activeTransferCount > 9 ? "9+" : controls.hostRoot.controller.activeTransferCount
                color: Theme.accentText
                font.family: Theme.uiFont
                font.pixelSize: 8
                font.weight: Font.Bold
            }
        }

        KeyboardAction {
            id: transferCenterAction

            objectName: "transferCenterAction"
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: qsTr("Open file transfers")
            onActivated: controls.transferPopup.visible ? controls.transferPopup.close() : controls.transferPopup.open()
        }

        AppToolTip {
            visible: transferCenterAction.hovered
            text: qsTr("File transfers")
        }
    }

    Rectangle {
        visible: !controls.compact
        width: visible ? controls.hostRoot.titleQuickActionWidth : 0
        height: controls.height
        color: commandPaletteAction.feedbackColor
        border.color: commandPaletteAction.visualFocus ? Theme.focus : "transparent"
        border.width: commandPaletteAction.visualFocus ? 1 : 0

        AppIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            name: "search"
            color: controls.commandPopup.visible ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        }

        KeyboardAction {
            id: commandPaletteAction

            objectName: "commandPaletteAction"
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: qsTr("Open command palette")
            onActivated: controls.commandPopup.open()
        }

        AppToolTip {
            visible: commandPaletteAction.hovered
            text: {
                const shortcut = controls.hostRoot.shortcutFor("application.commandPalette");
                return shortcut.length > 0 ? qsTr("Command palette") + " · " + shortcut : qsTr("Command palette");
            }
        }
    }

    Rectangle {
        width: controls.hostRoot.titleQuickActionWidth
        height: controls.height
        color: settingsShortcutAction.feedbackColor
        border.color: settingsShortcutAction.visualFocus ? Theme.focus : "transparent"
        border.width: settingsShortcutAction.visualFocus ? 1 : 0

        AppIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            name: controls.compact ? "more" : "settings"
            color: controls.hostRoot.currentPage === "settings" ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        }

        KeyboardAction {
            id: settingsShortcutAction

            objectName: "settingsShortcutAction"
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: controls.compact ? qsTr("More window actions") : qsTr("Open Settings")
            onActivated: controls.compact ? compactMenu.open() : controls.hostRoot.openSettingsTab()
        }
    }

    CaptionButton {
        objectName: "minimizeCaptionButton"
        width: controls.hostRoot.captionButtonWidth
        height: controls.height
        kind: "minimize"
        chrome: controls.hostRoot.windowChrome
        accessibleName: qsTr("Minimize")
        onActivated: controls.hostRoot.windowChrome.minimizeWindow()
    }

    CaptionButton {
        objectName: "maximizeCaptionButton"
        width: controls.hostRoot.captionButtonWidth
        height: controls.height
        kind: "maximize"
        chrome: controls.hostRoot.windowChrome
        accessibleName: controls.hostRoot.windowChrome.maximized ? qsTr("Restore") : qsTr("Maximize")
        externallyHovered: controls.hostRoot.windowChrome.maximizeButtonHovered
        externallyPressed: controls.hostRoot.windowChrome.maximizeButtonPressed
        onActivated: controls.hostRoot.windowChrome.toggleMaximize()
    }

    CaptionButton {
        objectName: "closeCaptionButton"
        width: controls.hostRoot.captionButtonWidth
        height: controls.height
        kind: "close"
        chrome: controls.hostRoot.windowChrome
        accessibleName: qsTr("Close")
        onActivated: controls.hostRoot.windowChrome.closeWindow()
    }
    AppMenu {
        id: compactMenu
        y: controls.height
        x: Math.max(0, controls.width - width)
        AppMenuItem {
            text: qsTr("Settings")
            iconName: "settings"
            onTriggered: controls.hostRoot.openSettingsTab()
        }
        AppMenuItem {
            text: qsTr("File transfers")
            iconName: "transfer"
            onTriggered: controls.transferPopup.open()
        }
        AppMenuItem {
            text: qsTr("Command palette")
            iconName: "search"
            onTriggered: controls.commandPopup.open()
        }
    }
}
