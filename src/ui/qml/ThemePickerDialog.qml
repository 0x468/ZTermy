pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Terminal theme picker (ADR 0121). Hovering a card previews the theme on
// every live terminal through the controller; Apply persists it, closing
// without applying restores the persisted theme.
Dialog {
    id: control

    property var controller: null
    property string selectedId: ""
    property string statusMessage: ""
    property bool statusIsError: false
    readonly property var themes: controller ? controller.terminalThemes : []
    readonly property var selectedTheme: themes.find(theme => theme.id === selectedId) || null
    readonly property bool selectedIsCustom: !!selectedTheme && !selectedTheme.builtIn

    signal themeApplied(string id)

    function openWithCurrent() {
        selectedId = controller ? controller.terminalThemeId : "";
        statusMessage = "";
        open();
    }

    function select(id) {
        selectedId = id;
        if (controller) {
            controller.previewTerminalTheme(id);
        }
    }

    function applySelection() {
        if (!controller || selectedId.length === 0) {
            return;
        }
        if (controller.saveTerminalTheme(selectedId)) {
            themeApplied(selectedId);
            accept();
        } else {
            statusMessage = qsTr("The theme could not be saved.");
            statusIsError = true;
        }
    }

    function importResultMessage(result) {
        if (result.ok) {
            return result.ids.length === 1 ? qsTr("Imported theme \"%1\".").arg(result.ids[0]) : qsTr("Imported %1 themes.").arg(result.ids.length);
        }
        switch (result.error) {
        case "unreadable":
            return qsTr("The file could not be read.");
        case "invalidColor":
            return qsTr("The file contains an invalid color.");
        case "writeFailed":
            return qsTr("The theme could not be written to the themes folder.");
        default:
            return qsTr("Unsupported theme format. Use a Windows Terminal scheme, a Ghostty theme or a ztermy theme file.");
        }
    }

    objectName: "themePickerDialog"
    anchors.centerIn: parent
    width: Math.min(760, Math.max(0, parent ? parent.width - 48 : 760))
    height: Math.min(600, Math.max(0, parent ? parent.height - 48 : 600))
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20

    onClosed: {
        if (controller) {
            controller.endTerminalThemePreview();
        }
    }

    enter: MotionEnter {}

    exit: MotionExit {}

    Overlay.modal: Rectangle {
        color: Theme.modalScrim
    }

    background: AppSurface {
        elevation: 3

        transform: Translate {
            y: control.visible ? 0 : Motion.distance

            Behavior on y {
                MotionRelocate {}
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingRelated
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Terminal theme")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingControl

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Terminal theme")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Hover a theme to preview it in every open terminal. Custom themes live in the themes folder next to your settings.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
            }

            ActionButton {
                objectName: "themePickerImport"
                text: qsTr("Import…")
                accessibleName: qsTr("Import a theme file")
                onClicked: importDialog.open()
            }

            ActionButton {
                objectName: "themePickerRemove"
                text: qsTr("Remove")
                accessibleName: qsTr("Remove the selected custom theme")
                enabled: control.selectedIsCustom
                variant: "destructive"
                onClicked: removeDialog.open()
            }
        }

        ScrollView {
            id: grid

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Flow {
                id: cards

                width: grid.availableWidth
                spacing: Theme.spacingRelated

                Repeater {
                    model: control.themes

                    delegate: Item {
                        id: card

                        required property var modelData
                        readonly property bool selected: control.selectedId === modelData.id
                        readonly property bool hovered: hover.hovered
                        readonly property color ink: modelData.foreground

                        width: Math.max(150, Math.floor((cards.width - (cards.spacing * 3)) / 4))
                        height: 118

                        AppSurface {
                            anchors.fill: parent
                            elevation: 1
                            color: card.modelData.background
                            border.color: card.selected ? Theme.accent : card.hovered ? Theme.borderStrong : Theme.border
                            border.width: card.selected ? 2 : 1

                            Behavior on border.color {
                                MotionColor {}
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: Theme.spacingControl

                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.name
                                    color: card.ink
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "$ ls -la ~/src"
                                    color: card.ink
                                    opacity: 0.72
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textLabel
                                }

                                Item {
                                    Layout.fillHeight: true
                                }

                                Row {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Repeater {
                                        model: card.modelData.ansi.slice(0, 8)

                                        delegate: Rectangle {
                                            required property var modelData

                                            width: 12
                                            height: 12
                                            radius: Theme.radiusSmall
                                            color: modelData
                                        }
                                    }

                                    Item {
                                        width: 6
                                        height: 1
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: card.modelData.builtIn ? "" : qsTr("custom")
                                        color: card.ink
                                        opacity: 0.6
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }
                            }
                        }

                        HoverHandler {
                            id: hover

                            onHoveredChanged: {
                                if (hovered && control.controller) {
                                    control.controller.previewTerminalTheme(card.modelData.id);
                                } else if (!hovered && control.controller && control.selectedId.length > 0) {
                                    control.controller.previewTerminalTheme(control.selectedId);
                                }
                            }
                        }

                        TapHandler {
                            onTapped: control.select(card.modelData.id)
                            onDoubleTapped: {
                                control.select(card.modelData.id);
                                control.applySelection();
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: card.modelData.name
                        Accessible.onPressAction: control.select(card.modelData.id)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingControl

            Text {
                Layout.fillWidth: true
                text: control.statusMessage.length > 0 ? control.statusMessage : control.selectedTheme ? qsTr("Selected: %1").arg(control.selectedTheme.name) : ""
                color: control.statusIsError && control.statusMessage.length > 0 ? Theme.dangerText : Theme.textMuted
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }

            ActionButton {
                id: cancelButton

                objectName: "themePickerCancel"
                text: qsTr("Cancel")
                accessibleName: qsTr("Cancel")
                KeyNavigation.right: applyButton
                onClicked: control.reject()
            }

            ActionButton {
                id: applyButton

                objectName: "themePickerApply"
                text: qsTr("Apply")
                accessibleName: qsTr("Apply the selected theme")
                enabled: control.selectedId.length > 0
                variant: "primary"
                KeyNavigation.left: cancelButton
                onClicked: control.applySelection()
            }
        }
    }

    FileDialog {
        id: importDialog

        title: qsTr("Import terminal theme")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Theme files (*.json *.conf *.txt *.ghostty)"), qsTr("All files (*)")]
        onAccepted: {
            const result = control.controller.importTerminalThemeFile(selectedFile);
            control.statusMessage = control.importResultMessage(result);
            control.statusIsError = !result.ok;
            if (result.ok && result.ids.length > 0) {
                control.select(result.ids[0]);
            }
        }
    }

    ConfirmationDialog {
        id: removeDialog

        heading: qsTr("Remove theme?")
        description: control.selectedTheme ? qsTr("\"%1\" will be deleted from the themes folder. Terminals using it fall back to the built-in default.").arg(control.selectedTheme.name) : ""
        acceptText: qsTr("Remove")
        destructive: true
        onAccepted: {
            const id = control.selectedId;
            if (control.controller.removeTerminalTheme(id)) {
                control.statusMessage = qsTr("Theme removed.");
                control.statusIsError = false;
                control.select(control.controller.terminalThemeId);
            } else {
                control.statusMessage = qsTr("The theme could not be removed.");
                control.statusIsError = true;
            }
        }
    }
}
