pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "settingsPane"
    required property var controller
    property bool loadingDraft: false
    property string statusMessage: ""
    property bool statusIsError: false
    readonly property bool draftDark: themeBox.currentIndex === 1 || (themeBox.currentIndex === 0 && Theme.systemDark)
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset

    color: Theme.workspaceBackground
    palette.base: Theme.raisedBackground
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.placeholderText: Theme.textMuted
    palette.button: Theme.controlBackground
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    function themeIndex(token) {
        return token === "system" ? 0 : token === "light" ? 2 : 1;
    }

    function backdropIndex(token) {
        return token === "mica" ? 1 : token === "acrylic" ? 2 : 0;
    }

    function cursorIndex(token) {
        return token === "block" ? 1 : token === "bar" ? 2 : token === "underline" ? 3 : 0;
    }

    function themeToken() {
        return themeBox.currentIndex === 0 ? "system" : themeBox.currentIndex === 2 ? "light" : "dark";
    }

    function backdropToken() {
        return backdropBox.currentIndex === 1 ? "mica" : backdropBox.currentIndex === 2 ? "acrylic" : "none";
    }

    function cursorToken() {
        return cursorBox.currentIndex === 1 ? "block" : cursorBox.currentIndex === 2 ? "bar" : cursorBox.currentIndex === 3 ? "underline" : "terminal";
    }

    function loadDraft() {
        loadingDraft = true;
        themeBox.currentIndex = themeIndex(controller.themePreference);
        opacitySlider.value = controller.windowOpacity;
        backdropBox.currentIndex = backdropIndex(controller.backdropPreference);
        fontFamilyField.text = controller.terminalFontFamily;
        fontSizeBox.value = controller.terminalFontSize;
        cursorBox.currentIndex = cursorIndex(controller.cursorPreference);
        cursorBlinkSwitch.checked = controller.cursorBlink;
        copyOnSelectSwitch.checked = controller.copyOnSelect;
        multilinePasteSwitch.checked = controller.confirmMultilinePaste;
        loadingDraft = false;
    }

    function applyDraft() {
        const saved = controller.saveApplicationSettings(themeToken(), opacitySlider.value, backdropToken(), fontFamilyField.text, fontSizeBox.value, cursorToken(), cursorBlinkSwitch.checked, copyOnSelectSwitch.checked, multilinePasteSwitch.checked);
        statusIsError = !saved;
        statusMessage = saved ? "Settings saved and applied." : "These settings could not be saved. Check the font and numeric ranges.";
        if (!saved) {
            loadDraft();
        }
    }

    onVisibleChanged: {
        if (visible) {
            loadDraft();
        }
    }
    Component.onCompleted: loadDraft()

    ScrollView {
        id: scrollView

        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight + 72

        ColumnLayout {
            id: contentColumn

            x: Math.max(pane.contentInset, (scrollView.availableWidth - width) / 2)
            y: pane.compactLayout ? 24 : 38
            width: Math.max(0, Math.min(920, scrollView.availableWidth - (pane.contentInset * 2)))
            spacing: Theme.spacingSection

            Text {
                text: "Settings"
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "Appearance and terminal preferences are stored locally for this ztermy data mode."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            SectionCard {
                Layout.fillWidth: true
                heading: "Window appearance"

                GridLayout {
                    id: appearanceLayout

                    objectName: "settingsAppearanceGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: "Theme"
                        color: Theme.text
                    }
                    ComboBox {
                        id: themeBox
                        Layout.fillWidth: true
                        model: ["System", "Dark", "Light"]
                        Accessible.name: "Application theme"
                    }

                    Label {
                        text: "Window opacity"
                        color: Theme.text
                    }
                    RowLayout {
                        Layout.fillWidth: true

                        Slider {
                            id: opacitySlider
                            Layout.fillWidth: true
                            from: 0.5
                            to: 1.0
                            stepSize: 0.05
                            Accessible.name: "Application window opacity"
                        }

                        Text {
                            Layout.preferredWidth: 42
                            horizontalAlignment: Text.AlignRight
                            text: Math.round(opacitySlider.value * 100) + "%"
                            color: Theme.textSoft
                            font.family: Theme.terminalFont
                            font.pixelSize: Theme.textLabel
                        }
                    }

                    Label {
                        text: "Windows backdrop"
                        color: Theme.text
                    }
                    ComboBox {
                        id: backdropBox
                        Layout.fillWidth: true
                        model: ["None", "Mica", "Acrylic"]
                        Accessible.name: "Windows backdrop material"
                    }

                    Item {
                        Layout.columnSpan: appearanceLayout.columns
                        Layout.fillWidth: true
                        implicitHeight: 52

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusControl
                            color: pane.draftDark ? "#111827" : "#FFFFFF"
                            border.color: pane.draftDark ? "#334155" : "#94A3B8"
                            opacity: opacitySlider.value

                            Row {
                                anchors.centerIn: parent
                                spacing: 8

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: pane.draftDark ? "#22C55E" : "#15803D"
                                }
                                Text {
                                    text: themeBox.currentText + " · " + backdropBox.currentText + " · preview"
                                    color: pane.draftDark ? "#F8FAFC" : "#0F172A"
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                heading: "Terminal"

                GridLayout {
                    id: terminalLayout

                    objectName: "settingsTerminalGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: "Font family"
                        color: Theme.text
                    }
                    AppTextField {
                        id: fontFamilyField
                        Layout.fillWidth: true
                        placeholderText: "Cascadia Mono"
                        selectByMouse: true
                        maximumLength: 128
                        Accessible.name: "Terminal font family"
                    }

                    Label {
                        text: "Font size"
                        color: Theme.text
                    }
                    SpinBox {
                        id: fontSizeBox
                        Layout.fillWidth: true
                        from: 8
                        to: 32
                        editable: true
                        Accessible.name: "Terminal font size"
                    }

                    Label {
                        text: "Cursor"
                        color: Theme.text
                    }
                    ComboBox {
                        id: cursorBox
                        Layout.fillWidth: true
                        model: ["Terminal controlled", "Block", "Bar", "Underline"]
                        Accessible.name: "Terminal cursor style"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: cursorBlinkSwitch.implicitHeight
                    }
                    Switch {
                        id: cursorBlinkSwitch
                        Layout.fillWidth: true
                        text: "Blink cursor"
                        Accessible.name: "Blink terminal cursor"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: copyOnSelectSwitch.implicitHeight
                    }
                    Switch {
                        id: copyOnSelectSwitch
                        Layout.fillWidth: true
                        text: "Copy selected terminal text automatically"
                        Accessible.name: "Copy terminal selection automatically"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: multilinePasteSwitch.implicitHeight
                    }
                    Switch {
                        id: multilinePasteSwitch
                        Layout.fillWidth: true
                        text: "Confirm before pasting multiple lines"
                        Accessible.name: "Confirm multiline terminal paste"
                    }
                }
            }

            StatusMessage {
                Layout.fillWidth: true
                text: pane.statusMessage
                kind: pane.statusIsError ? "error" : "success"
            }

            GridLayout {
                Layout.fillWidth: true
                columns: pane.compactLayout ? 1 : 4
                columnSpacing: Theme.spacingControl
                rowSpacing: Theme.spacingControl

                ActionButton {
                    Layout.fillWidth: pane.compactLayout
                    text: "Reset defaults"
                    Accessible.name: "Reset all application settings"
                    onClicked: {
                        const reset = pane.controller.resetApplicationSettings();
                        pane.statusIsError = !reset;
                        pane.statusMessage = reset ? "Default settings restored." : "Default settings could not be restored.";
                        pane.loadDraft();
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: !pane.compactLayout
                }

                ActionButton {
                    Layout.fillWidth: pane.compactLayout
                    text: "Discard changes"
                    Accessible.name: "Discard unsaved setting changes"
                    onClicked: {
                        pane.loadDraft();
                        pane.statusIsError = false;
                        pane.statusMessage = "Unsaved changes discarded.";
                    }
                }

                ActionButton {
                    Layout.fillWidth: pane.compactLayout
                    text: "Apply"
                    Accessible.name: "Apply application settings"
                    variant: "primary"
                    onClicked: pane.applyDraft()
                }
            }
        }
    }
}
