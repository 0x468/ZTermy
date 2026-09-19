pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var controller
    property string mode: "fixed"
    property string fixedTheme: "ztermy-dark"
    property string lightTheme: "ztermy-light"
    property string darkTheme: "ztermy-dark"
    property bool darkSlot: Theme.systemDark
    property bool editingSlot: false
    property string hoverId: ""
    property int filter: 0
    property string message: ""
    readonly property string chosenId: mode === "fixed" ? fixedTheme : darkSlot ? darkTheme : lightTheme
    readonly property string effectiveDraftId: mode === "fixed" ? fixedTheme : Theme.systemDark ? darkTheme : lightTheme
    readonly property var themes: controller.terminalThemes
    readonly property var candidates: themes.filter(t => mode === "system" ? t.dark === darkSlot : filter === 0 || (filter === 1 ? !t.dark : filter === 2 ? t.dark : !t.builtIn))
    readonly property var previewData: controller.terminalThemeColorsFor(hoverId || (editingSlot ? chosenId : effectiveDraftId))
    signal edited
    signal cardFocused(var item)
    spacing: 12

    function loadPolicy() {
        const policy = controller.themePolicy;
        mode = policy.mode;
        fixedTheme = policy.fixed;
        lightTheme = policy.light;
        darkTheme = policy.dark;
        darkSlot = Theme.systemDark;
        editingSlot = false;
        hoverId = "";
    }
    function previewDraft() {
        hoverId = "";
        controller.previewTerminalTheme(editingSlot ? chosenId : effectiveDraftId);
        edited();
    }
    function choose(id) {
        if (mode === "fixed")
            fixedTheme = id;
        else if (darkSlot)
            darkTheme = id;
        else
            lightTheme = id;
        editingSlot = true;
        previewDraft();
    }
    function save() {
        const ok = controller.saveThemePolicy(mode, fixedTheme, lightTheme, darkTheme);
        if (ok) {
            editingSlot = false;
            hoverId = "";
        }
        return ok;
    }
    function themeName(id) {
        return controller.terminalThemeColorsFor(id).name || id;
    }
    Connections {
        target: Theme
        function onSystemDarkChanged() {
            if (editor.visible && !editor.editingSlot && editor.mode === "system")
                editor.previewDraft();
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ActionButton {
            objectName: "settingsThemeSystem"
            text: qsTr("Follow system")
            variant: editor.mode === "system" ? "primary" : "secondary"
            onClicked: {
                editor.mode = "system";
                editor.editingSlot = false;
                editor.previewDraft();
            }
        }
        ActionButton {
            objectName: "settingsThemeFixed"
            text: qsTr("Fixed theme")
            variant: editor.mode === "fixed" ? "primary" : "secondary"
            onClicked: {
                editor.mode = "fixed";
                editor.editingSlot = false;
                editor.previewDraft();
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        visible: editor.mode === "system"
        ActionButton {
            objectName: "settingsThemeLightSlot"
            text: qsTr("Light: %1").arg(editor.themeName(editor.lightTheme))
            variant: !editor.darkSlot ? "primary" : "secondary"
            onClicked: {
                editor.darkSlot = false;
                editor.editingSlot = true;
                editor.previewDraft();
            }
        }
        ActionButton {
            objectName: "settingsThemeDarkSlot"
            text: qsTr("Dark: %1").arg(editor.themeName(editor.darkTheme))
            variant: editor.darkSlot ? "primary" : "secondary"
            onClicked: {
                editor.darkSlot = true;
                editor.editingSlot = true;
                editor.previewDraft();
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: editor.mode === "system" ? qsTr("System %1 → %2").arg(Theme.systemDark ? qsTr("dark") : qsTr("light")).arg(editor.themeName(editor.effectiveDraftId)) : qsTr("The selected theme stays active regardless of the system appearance.")
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: editor.mode === "fixed"
        Repeater {
            model: [qsTr("All"), qsTr("Light"), qsTr("Dark"), qsTr("Custom")]
            ActionButton {
                required property int index
                required property string modelData
                text: modelData
                variant: editor.filter === index ? "primary" : "secondary"
                onClicked: editor.filter = index
            }
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: editor.width >= 720 ? 2 : 1
        columnSpacing: 16
        rowSpacing: 12
        Flow {
            id: cards
            Layout.fillWidth: true
            Layout.preferredWidth: 340
            Layout.alignment: Qt.AlignTop
            spacing: 8
            Repeater {
                model: editor.candidates
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    width: Math.max(100, (cards.width - 8) / 2)
                    height: 126
                    radius: Theme.radiusControl
                    color: Theme.panelBackground
                    border.color: cardAction.visualFocus ? Theme.focus : modelData.id === editor.chosenId ? Theme.accent : Theme.border
                    border.width: modelData.id === editor.chosenId || cardAction.visualFocus ? 2 : 1
                    ThemeWindowPreview {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 7
                        height: 78
                        miniature: true
                        themeData: card.modelData
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 9
                        text: card.modelData.name
                        color: Theme.text
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                    KeyboardAction {
                        id: cardAction
                        objectName: "themeCard-" + card.modelData.id
                        anchors.fill: parent
                        accessibleName: card.modelData.name
                        onActivated: editor.choose(card.modelData.id)
                        onHoveredChanged: {
                            if (hovered)
                                editor.hoverId = card.modelData.id;
                            else if (editor.hoverId === card.modelData.id && !visualFocus)
                                editor.hoverId = "";
                        }
                        onVisualFocusChanged: {
                            if (visualFocus) {
                                editor.hoverId = card.modelData.id;
                                editor.cardFocused(card);
                            } else if (editor.hoverId === card.modelData.id && !hovered)
                                editor.hoverId = "";
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 350
            Layout.alignment: Qt.AlignTop
            ThemeWindowPreview {
                objectName: "settingsThemePreview"
                Layout.fillWidth: true
                themeData: editor.previewData
            }
            Text {
                Layout.fillWidth: true
                text: editor.themeName(editor.previewData.id || editor.chosenId)
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Hover to preview here. Select a card to preview the whole application; Apply below saves it.")
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
            ActionButton {
                text: qsTr("End preview")
                visible: editor.mode === "system" && editor.editingSlot
                onClicked: {
                    editor.editingSlot = false;
                    editor.previewDraft();
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        ActionButton {
            text: qsTr("Import…")
            onClicked: importDialog.open()
        }
        ActionButton {
            text: qsTr("Remove")
            enabled: !!editor.controller.terminalThemeColorsFor(editor.chosenId).id && !editor.controller.terminalThemeColorsFor(editor.chosenId).builtIn
            onClicked: removeDialog.open()
        }
    }
    Text {
        Layout.fillWidth: true
        text: editor.message || qsTr("Import and Remove change the theme library immediately.")
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import theme")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Theme files (*.json *.conf *.txt *.ghostty)"), qsTr("All files (*)")]
        onAccepted: {
            const result = editor.controller.importTerminalThemeFile(selectedFile);
            if (result.ok && result.ids.length) {
                const id = result.ids[0];
                if (editor.mode === "system")
                    editor.darkSlot = !!editor.controller.terminalThemeColorsFor(id).dark;
                editor.choose(id);
                editor.message = qsTr("Theme imported.");
            } else
                editor.message = qsTr("Could not import the theme. Check the format and file access.");
        }
    }
    ConfirmationDialog {
        id: removeDialog
        heading: qsTr("Remove theme?")
        description: qsTr("The custom theme file will be deleted. Slots using it return to their built-in default.")
        acceptText: qsTr("Remove")
        destructive: true
        onAccepted: {
            const id = editor.chosenId;
            if (!editor.controller.removeTerminalTheme(id)) {
                editor.message = qsTr("Could not remove the theme.");
                return;
            }
            if (editor.lightTheme === id)
                editor.lightTheme = "ztermy-light";
            if (editor.darkTheme === id)
                editor.darkTheme = "ztermy-dark";
            if (editor.fixedTheme === id)
                editor.fixedTheme = "ztermy-dark";
            editor.previewDraft();
            editor.message = qsTr("Theme removed.");
        }
    }
}
