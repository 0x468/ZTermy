pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window

RowLayout {
    id: root
    required property var controller
    required property string paneId
    property string paneTitle: ""
    property int paneCount: 1
    readonly property var hostWindow: root.Window.window
    readonly property bool detachedWindow: hostWindow && hostWindow.windowControlsVisible !== undefined
    readonly property bool windowControlsVisible: detachedWindow && hostWindow.windowControlsVisible
    property bool zoomed: false
    property bool revealed: windowControlsVisible
    readonly property bool interactionActive: hover.hovered || activeFocus || newPaneMenu.visible
    signal zoomRequested
    signal detachRequested
    signal toggleWindowControlsRequested
    spacing: 2
    opacity: revealed ? 1 : 0
    enabled: revealed

    Behavior on opacity {
        NumberAnimation {
            duration: Motion.enabled ? 70 : 0
            easing.type: Easing.OutQuad
        }
    }
    HoverHandler {
        id: hover
    }

    function createPane(profile, shell, copy) {
        if (controller.activateTerminalPane(paneId))
            controller.splitActiveTerminal("auto", copy, profile, shell);
    }

    // The model only carries stable identity; labels and visibility are
    // bound inside the delegate so toggling headers, zoom or pane count
    // does not rebuild the array and re-create every button.
    Repeater {
        model: ["headers", "drag", "zoom", "detach", "copy", "new", "close"]
        delegate: AppIconButton {
            id: button
            required property string modelData
            property string dragPaneId: root.paneId
            property string dragPaneTitle: root.paneTitle
            property bool dragWindow: modelData === "headers" && root.detachedWindow
            objectName: "terminalPaneAction-" + modelData + "-" + root.paneId
            visible: {
                switch (modelData) {
                case "drag":
                    return root.detachedWindow && root.paneCount > 1;
                case "zoom":
                case "close":
                    return root.paneCount > 1;
                default:
                    return true;
                }
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            label: {
                switch (modelData) {
                case "headers":
                    return root.detachedWindow ? (root.windowControlsVisible ? qsTr("Hide window controls — drag to move window") : qsTr("Show window controls — drag to move window")) : qsTr("Drag pane");
                case "drag":
                    return qsTr("Drag pane");
                case "zoom":
                    return root.zoomed ? qsTr("Restore pane layout") : qsTr("Zoom this pane within its tab");
                case "detach":
                    return qsTr("Detach terminal pane");
                case "copy":
                    return qsTr("Copy pane — new session, same profile or Shell");
                case "new":
                    return qsTr("New pane — choose a host or local Shell");
                default:
                    return qsTr("Close this pane");
                }
            }
            iconName: modelData === "headers" || modelData === "drag" ? "list" : modelData === "zoom" ? "locate" : modelData === "detach" ? "external-link" : modelData === "copy" ? "copy" : modelData === "new" ? "plus" : "close"
            selected: (modelData === "headers" && root.windowControlsVisible) || (modelData === "zoom" && root.zoomed)
            onWorkspace: true
            iconColor: selected ? Theme.accent : Theme.workspaceText
            toolTipEnabled: !newPaneMenu.visible
            onClicked: {
                switch (modelData) {
                case "headers":
                    if (root.detachedWindow)
                        root.toggleWindowControlsRequested();
                    break;
                case "zoom":
                    root.zoomRequested();
                    break;
                case "detach":
                    root.detachRequested();
                    break;
                case "copy":
                    root.createPane("", "", true);
                    break;
                case "new":
                    newPaneMenu.popup(button, 0, button.height);
                    break;
                case "close":
                    if (root.controller.activateTerminalPane(root.paneId))
                        root.controller.closeActiveTerminalPane();
                    break;
                }
            }
        }
    }
    AppMenu {
        id: newPaneMenu
        objectName: "terminalNewPaneMenu-" + root.paneId
        // Host and shell entries are only instantiated once the menu is first
        // opened; every pane used to build the full list on creation.
        property bool populated: false
        onAboutToShow: populated = true
        AppMenuItem {
            text: qsTr("Default local Shell")
            iconName: "terminal"
            onTriggered: root.createPane("", "", false)
        }
        Instantiator {
            active: newPaneMenu.populated
            model: root.controller.availableLocalShells.filter(shell => shell.id !== "automatic" && shell.available)
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                objectName: "newPaneShell-" + modelData.id
                iconName: modelData.iconName || "terminal"
                onTriggered: root.createPane("", modelData.id, false)
            }
            onObjectAdded: (index, object) => newPaneMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => newPaneMenu.removeItem(object)
        }
        AppMenuSeparator {}
        Instantiator {
            active: newPaneMenu.populated
            model: root.controller.hostProfiles
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                iconName: modelData.iconName || "hosts"
                onTriggered: root.createPane(modelData.id, "", false)
            }
            onObjectAdded: (index, object) => newPaneMenu.addItem(object)
            onObjectRemoved: (index, object) => newPaneMenu.removeItem(object)
        }
    }
}
