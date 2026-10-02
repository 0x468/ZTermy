pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

SectionCard {
    id: settings
    required property var controller
    property string menuMode: "single"
    property string singleShell: "automatic"
    property var selectedShells: []
    readonly property bool hasUnsavedChanges: {
        const saved = controller.windowsIntegrationSettings;
        return menuMode !== saved.menuMode || singleShell !== saved.singleShell || selectedShells.length !== saved.submenuShells.length || selectedShells.some(shell => saved.submenuShells.indexOf(shell) < 0);
    }
    readonly property var shells: [
        {
            id: "automatic",
            name: qsTr("Default local shell")
        }
    ].concat(controller.availableLocalShells.filter(shell => shell.available && shell.id !== "automatic"))
    heading: qsTr("Windows integration")
    objectName: "settingsWindowsIntegrationCard"

    function load(values) {
        menuMode = values.menuMode;
        singleShell = values.singleShell;
        selectedShells = values.submenuShells.slice();
    }
    function values() {
        return {
            menuMode: menuMode,
            singleShell: singleShell,
            submenuShells: selectedShells
        };
    }
    function selectShell(id, selected) {
        const updated = selectedShells.filter(shell => shell !== id);
        if (selected)
            updated.push(id);
        if (updated.length > 0)
            selectedShells = updated;
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingControl

        Text {
            Layout.fillWidth: true
            text: qsTr("Choose the shells shown in Explorer. Registration is managed by the installer; these choices apply to both classic and Windows 11 menus after saving.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }
        SettingsRowLabel {
            Layout.fillWidth: true
            text: qsTr("Explorer menu style")
        }
        AppComboBox {
            Layout.fillWidth: true
            objectName: "settingsExplorerMenuMode"
            model: ["single", "submenu"]
            displayTextModel: [qsTr("Single entry"), qsTr("Shell submenu")]
            currentIndex: settings.menuMode === "submenu" ? 1 : 0
            accessibleName: qsTr("Explorer menu style")
            onActivated: index => settings.menuMode = model[index]
        }
        SettingsRowLabel {
            Layout.fillWidth: true
            visible: settings.menuMode === "single"
            text: qsTr("Shell opened by the single entry")
        }
        AppComboBox {
            Layout.fillWidth: true
            visible: settings.menuMode === "single"
            objectName: "settingsExplorerSingleShell"
            model: settings.shells.map(shell => shell.id)
            displayTextModel: settings.shells.map(shell => shell.name)
            currentIndex: Math.max(0, model.indexOf(settings.singleShell))
            accessibleName: qsTr("Shell opened by the single entry")
            onActivated: index => settings.singleShell = model[index]
        }
        Repeater {
            model: settings.menuMode === "submenu" ? settings.shells : []
            AppSwitch {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.name
                accessibleName: text
                checked: settings.selectedShells.indexOf(modelData.id) >= 0
                enabled: !checked || settings.selectedShells.length > 1
                onToggled: settings.selectShell(modelData.id, checked)
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Only installed local shells are listed. If all selected shells become unavailable, the default local shell remains available.")
            color: Theme.textSubtle
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }
    }
}
