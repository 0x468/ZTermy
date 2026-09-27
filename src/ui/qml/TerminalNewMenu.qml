pragma ComponentBehavior: Bound
import QtQuick

AppMenu {
    id: menu
    required property var controller
    property bool mainWindowActions: false
    signal localRequested(string shellId)
    signal hostRequested(var profile)
    signal manageHostsRequested
    signal reopenRequested

    AppMenuItem {
        objectName: "newLocalTerminalMenuAction"
        text: qsTranslate("Main", "New local terminal")
        onTriggered: menu.localRequested("")
    }
    AppMenu {
        id: shells
        objectName: "newTerminalShellMenu"
        title: qsTranslate("Main", "New terminal with")
        Instantiator {
            model: menu.controller.availableLocalShells.filter(shell => shell.id !== "automatic" && shell.available)
            delegate: AppMenuItem {
                id: shellItem
                required property var modelData
                text: modelData.name
                onTriggered: menu.localRequested(modelData.id)
                AppToolTip {
                    text: shellItem.modelData.detail
                }
            }
            onObjectAdded: (index, object) => shells.insertItem(index, object)
            onObjectRemoved: (index, object) => shells.removeItem(object)
        }
    }
    AppMenu {
        id: hosts
        objectName: "newTerminalHostsMenu"
        title: qsTranslate("Main", "Saved hosts")
        enabled: menu.controller.hostProfiles.length > 0
        Instantiator {
            model: menu.controller.hostProfiles
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: menu.hostRequested(modelData)
            }
            onObjectAdded: (index, object) => hosts.insertItem(index, object)
            onObjectRemoved: (index, object) => hosts.removeItem(object)
        }
    }
    AppMenuItem {
        objectName: "browseHostsMenuAction"
        visible: menu.mainWindowActions
        text: qsTranslate("Main", "Manage hosts")
        onTriggered: menu.manageHostsRequested()
    }
    AppMenuSeparator {
        visible: menu.mainWindowActions && menu.controller.canReopenClosedTerminalTab
    }
    AppMenuItem {
        visible: menu.mainWindowActions && menu.controller.canReopenClosedTerminalTab
        text: qsTranslate("Main", "Reopen closed terminal")
        onTriggered: menu.reopenRequested()
    }
}
