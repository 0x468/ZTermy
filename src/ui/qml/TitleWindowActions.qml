pragma ComponentBehavior: Bound

import QtQuick

// Right-hand title-bar group: portable vault status, pin, file transfers,
// command palette, settings, then the three window caption buttons.
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

    TitleChromeAction {
        id: portableVaultStatus

        width: controls.hostRoot.titleSecurityActionWidth
        height: controls.height
        visible: controls.hostRoot.portableVaultNeedsAttention
        iconName: "lock"
        iconColor: Theme.dangerText
        actionObjectName: "portableVaultStatusAction"
        accessibleName: controls.hostRoot.controller.portableVaultInitialized ? qsTr("Portable vault locked; unlock") : qsTr("Portable vault not configured; open Security settings")
        onActivated: controls.hostRoot.requestPortableVaultAccess(portableVaultStatus.focusTarget)

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.top: parent.top
            anchors.topMargin: 7
            width: 6
            height: 6
            radius: height / 2
            color: Theme.danger
            border.color: Theme.chromeBackground
            border.width: 1
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

            TitleChromeAction {
                id: alwaysOnTopAction

                width: 26
                height: parent.height
                iconName: controls.hostRoot.windowAlwaysOnTopRequested ? "pin-window" : controls.hostRoot.currentTerminalTabPinned ? "pin-tab" : "pin"
                iconColor: controls.hostRoot.windowAlwaysOnTopRequested || controls.hostRoot.currentTerminalTabPinned ? Theme.accent : controls.hostRoot.mutedColor
                actionObjectName: "alwaysOnTopAction"
                doubleClickEnabled: true
                menuOpen: alwaysOnTopMenu.visible
                accessibleName: controls.hostRoot.windowAlwaysOnTopRequested ? qsTr("Turn off window always on top") : controls.hostRoot.currentPage !== "terminal" || controls.hostRoot.activeTerminalTab === null ? qsTr("Keep window always on top") : controls.hostRoot.currentTerminalTabPinned ? qsTr("Unpin current terminal tab") : qsTr("Pin current terminal tab")
                toolTip: {
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
                onActivated: controls.hostRoot.activatePinPrimary()
                onDoubleActivated: controls.hostRoot.activatePinDouble()
            }

            TitleChromeAction {
                width: 14
                height: parent.height
                iconName: "chevron-down"
                iconSize: 10
                iconColor: controls.hostRoot.mutedColor
                actionInset: 0
                actionObjectName: "alwaysOnTopMenuAction"
                accessibleName: qsTr("Open pin options")
                toolTip: qsTr("Pin options")
                menuOpen: alwaysOnTopMenu.visible
                onActivated: alwaysOnTopMenu.open()
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

    TitleChromeAction {
        visible: !controls.compact
        width: visible ? controls.hostRoot.titleQuickActionWidth : 0
        height: controls.height
        iconName: "transfer"
        iconColor: controls.transferPopup.visible ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        actionObjectName: "transferCenterAction"
        accessibleName: qsTr("Open file transfers")
        toolTip: qsTr("File transfers")
        onActivated: controls.transferPopup.visible ? controls.transferPopup.close() : controls.transferPopup.open()

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 5
            visible: controls.hostRoot.controller.activeTransferCount > 0
            width: Math.max(12, transferCountText.implicitWidth + 4)
            height: 12
            radius: height / 2
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
    }

    TitleChromeAction {
        visible: !controls.compact
        width: visible ? controls.hostRoot.titleQuickActionWidth : 0
        height: controls.height
        iconName: "search"
        iconColor: controls.commandPopup.visible ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        actionObjectName: "commandPaletteAction"
        accessibleName: qsTr("Open command palette")
        toolTip: {
            const shortcut = controls.hostRoot.shortcutFor("application.commandPalette");
            return shortcut.length > 0 ? qsTr("Command palette") + " · " + shortcut : qsTr("Command palette");
        }
        onActivated: controls.commandPopup.open()
    }

    TitleChromeAction {
        width: controls.hostRoot.titleQuickActionWidth
        height: controls.height
        iconName: controls.compact ? "more" : "settings"
        iconColor: controls.hostRoot.currentPage === "settings" ? controls.hostRoot.textColor : controls.hostRoot.mutedColor
        actionObjectName: "settingsShortcutAction"
        accessibleName: controls.compact ? qsTr("More window actions") : qsTr("Open Settings")
        toolTip: controls.compact ? qsTr("More window actions") : qsTr("Settings")
        menuOpen: compactMenu.visible
        onActivated: controls.compact ? compactMenu.open() : controls.hostRoot.openSettingsTab()
    }

    CaptionButton {
        objectName: "minimizeCaptionButton"
        width: controls.hostRoot.captionButtonWidth
        height: controls.height
        kind: "minimize"
        chrome: controls.hostRoot.windowChrome
        accessibleName: qsTr("Minimize")
        onActivated: WindowControl.minimize(controls.hostRoot.windowChrome)
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
        onActivated: WindowControl.toggleMaximize(controls.hostRoot.windowChrome)
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
