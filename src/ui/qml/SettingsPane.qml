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
    readonly property bool adjustableBackdrop: backdropBox.currentIndex === 0 || backdropBox.currentIndex === 1
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset

    signal appearancePreviewRequested(string theme, real opacity, string backdrop)
    signal appearancePreviewEnded

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
        return token === "transparent" ? 1 : token === "mica" ? 2 : token === "micaAlt" ? 3 : 0;
    }

    function cursorIndex(token) {
        return token === "block" ? 1 : token === "bar" ? 2 : token === "underline" ? 3 : 0;
    }

    function themeToken() {
        return themeBox.currentIndex === 0 ? "system" : themeBox.currentIndex === 2 ? "light" : "dark";
    }

    function backdropToken() {
        return backdropBox.currentIndex === 1 ? "transparent" : backdropBox.currentIndex === 2 ? "mica" : backdropBox.currentIndex === 3 ? "micaAlt" : "acrylic";
    }

    function cursorToken() {
        return cursorBox.currentIndex === 1 ? "block" : cursorBox.currentIndex === 2 ? "bar" : cursorBox.currentIndex === 3 ? "underline" : "terminal";
    }

    function previewDraft() {
        if (!visible || loadingDraft) {
            return;
        }
        appearancePreviewRequested(themeToken(), opacitySlider.value, backdropToken());
    }

    function loadDraft() {
        loadingDraft = true;
        themeBox.currentIndex = themeIndex(controller.themePreference);
        opacitySlider.value = controller.backdropOpacity;
        backdropBox.currentIndex = backdropIndex(controller.backdropPreference);
        fontFamilyField.text = controller.terminalFontFamily;
        fontSizeBox.value = controller.terminalFontSize;
        cursorBox.currentIndex = cursorIndex(controller.cursorPreference);
        cursorBlinkSwitch.checked = controller.cursorBlink;
        copyOnSelectSwitch.checked = controller.copyOnSelect;
        multilinePasteSwitch.checked = controller.confirmMultilinePaste;
        loadingDraft = false;
        previewDraft();
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
        } else {
            appearancePreviewEnded();
        }
    }
    Component.onCompleted: loadDraft()

    ScrollView {
        id: scrollView

        anchors.fill: parent
        anchors.rightMargin: 8
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
                    AppComboBox {
                        id: themeBox
                        objectName: "settingsTheme"
                        Layout.fillWidth: true
                        model: ["System", "Dark", "Light"]
                        accessibleName: "Application theme"
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        text: "Windows backdrop"
                        color: Theme.text
                    }
                    AppComboBox {
                        id: backdropBox
                        objectName: "settingsBackdrop"
                        Layout.fillWidth: true
                        model: ["Acrylic", "Transparent", "Mica", "Mica Alt"]
                        accessibleName: "Windows backdrop material"
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        visible: pane.adjustableBackdrop
                        text: "Background opacity"
                        color: Theme.text
                    }
                    RowLayout {
                        visible: pane.adjustableBackdrop
                        Layout.fillWidth: true

                        AppSlider {
                            id: opacitySlider
                            objectName: "settingsOpacity"
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            stepSize: 0.05
                            accessibleName: "Window background opacity"
                            onValueChanged: pane.previewDraft()
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

                    Item {
                        Layout.columnSpan: appearanceLayout.columns
                        Layout.fillWidth: true
                        implicitHeight: 52

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusControl
                            color: {
                                const alpha = pane.adjustableBackdrop ? opacitySlider.value : backdropBox.currentIndex === 2 ? 0.82 : 0.88;
                                return pane.draftDark ? Qt.rgba(0.067, 0.094, 0.153, alpha) : Qt.rgba(1.0, 1.0, 1.0, alpha);
                            }
                            border.color: pane.draftDark ? "#334155" : "#94A3B8"

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 8

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: pane.draftDark ? "#22C55E" : "#15803D"
                                }
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: themeBox.currentText + " · " + backdropBox.currentText + (pane.adjustableBackdrop ? " · " + Math.round(opacitySlider.value * 100) + "%" : " · system controlled")
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
                        objectName: "settingsFontFamily"
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
                    AppSpinBox {
                        id: fontSizeBox
                        objectName: "settingsFontSize"
                        Layout.fillWidth: true
                        from: 8
                        to: 32
                        editable: true
                        accessibleName: "Terminal font size"
                    }

                    Label {
                        text: "Cursor"
                        color: Theme.text
                    }
                    AppComboBox {
                        id: cursorBox
                        objectName: "settingsCursor"
                        Layout.fillWidth: true
                        model: ["Terminal controlled", "Block", "Bar", "Underline"]
                        accessibleName: "Terminal cursor style"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: cursorBlinkSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: cursorBlinkSwitch
                        objectName: "settingsCursorBlink"
                        Layout.fillWidth: true
                        text: "Blink cursor"
                        accessibleName: "Blink terminal cursor"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: copyOnSelectSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: copyOnSelectSwitch
                        objectName: "settingsCopyOnSelect"
                        Layout.fillWidth: true
                        text: "Copy selected terminal text automatically"
                        accessibleName: "Copy terminal selection automatically"
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: multilinePasteSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: multilinePasteSwitch
                        objectName: "settingsMultilinePaste"
                        Layout.fillWidth: true
                        text: "Confirm before pasting multiple lines"
                        accessibleName: "Confirm multiline terminal paste"
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
                    objectName: "settingsReset"
                    Layout.fillWidth: pane.compactLayout
                    text: "Reset defaults"
                    accessibleName: "Reset all application settings"
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
                    objectName: "settingsDiscard"
                    Layout.fillWidth: pane.compactLayout
                    text: "Discard changes"
                    accessibleName: "Discard unsaved setting changes"
                    onClicked: {
                        pane.loadDraft();
                        pane.statusIsError = false;
                        pane.statusMessage = "Unsaved changes discarded.";
                    }
                }

                ActionButton {
                    objectName: "settingsApply"
                    Layout.fillWidth: pane.compactLayout
                    text: "Apply"
                    accessibleName: "Apply application settings"
                    variant: "primary"
                    onClicked: pane.applyDraft()
                }
            }
        }
    }
}
