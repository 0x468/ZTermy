pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "settingsPane"
    required property var controller
    required property var fontCatalog
    property bool loadingDraft: false
    property string statusMessage: ""
    property bool statusIsError: false
    property string currentCategory: "application"
    property string languageDraft: "system"
    property string uiFontDraft: ""
    property string terminalFontDraft: "Cascadia Mono"
    property real contentReveal: 1.0
    readonly property bool shortcutRecording: shortcutSettings.recording
    readonly property bool draftDark: themeBox.currentIndex === 1 || (themeBox.currentIndex === 0 && Theme.systemDark)
    readonly property bool adjustableBackdrop: backdropBox.currentIndex === 0 || backdropBox.currentIndex === 1
    readonly property bool customAccentSelected: accentBox.currentIndex === 2
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset
    readonly property bool terminalLigatureAvailable: fontCatalog.supportsLigatures(terminalFontDraft)
    readonly property bool uiFontHasCjk: fontCatalog.supportsCjk(uiFontDraft)
    readonly property var uiFontOptions: systemFontOptions(fontCatalog.allFamilies)
    readonly property var terminalFontOptions: visibleTerminalFonts(showAllFontsSwitch.checked, terminalFontDraft)

    signal appearancePreviewRequested(string theme, real opacity, string backdrop, string accent, string customAccent)
    signal appearancePreviewEnded

    component CategoryButton: Rectangle {
        id: categoryControl

        required property string title
        required property string iconName
        property bool selected: false
        property string actionObjectName: ""
        signal activated

        implicitHeight: 40
        radius: Theme.radiusControl
        color: selected ? Theme.controlBackground : (categoryAction.hovered || categoryAction.activeFocus ? Theme.controlHover : "transparent")
        border.color: categoryAction.activeFocus ? Theme.focus : "transparent"
        border.width: categoryAction.activeFocus ? 1 : 0

        function focusAction() {
            categoryAction.forceActiveFocus();
        }

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 18
            radius: 2
            visible: categoryControl.selected
            color: Theme.accent
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            AppIcon {
                width: 16
                height: 16
                name: categoryControl.iconName
                color: categoryControl.selected ? Theme.text : Theme.textMuted
            }

            Text {
                text: categoryControl.title
                color: categoryControl.selected ? Theme.text : Theme.textSoft
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
                font.weight: categoryControl.selected ? Font.DemiBold : Font.Normal
            }
        }

        KeyboardAction {
            id: categoryAction

            objectName: categoryControl.actionObjectName
            anchors.fill: parent
            anchors.margins: 2
            accessibleName: qsTr("%1 settings").arg(categoryControl.title)
            onActivated: categoryControl.activated()
        }
    }

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

    function accentIndex(token) {
        return token === "system" ? 1 : token === "custom" ? 2 : 0;
    }

    function credentialStorageIndex(token) {
        return token === "portable" ? 1 : token === "session" ? 2 : 0;
    }

    function languageIndex(token) {
        return token === "en" ? 1 : token === "zh_CN" ? 2 : 0;
    }

    function languageToken(index) {
        return index === 1 ? "en" : index === 2 ? "zh_CN" : "system";
    }

    function systemFontOptions(families) {
        const result = [""];
        for (let index = 0; index < families.length; ++index) {
            result.push(families[index]);
        }
        return result;
    }

    function visibleTerminalFonts(showAll, selected) {
        const source = showAll ? fontCatalog.allFamilies : fontCatalog.monospacedFamilies;
        const result = [];
        if (selected.length > 0 && source.indexOf(selected) < 0) {
            result.push(selected);
        }
        for (let index = 0; index < source.length; ++index) {
            result.push(source[index]);
        }
        return result;
    }

    function credentialStorageToken() {
        return credentialStorageTokenForIndex(credentialStorageBox.currentIndex);
    }

    function credentialStorageTokenForIndex(index) {
        return index === 1 ? "portable" : index === 2 ? "session" : "system";
    }

    function credentialStorageLabel(token) {
        return token === "portable" ? qsTr("Portable encrypted vault") : token === "session" ? qsTr("Session only") : qsTr("Windows Credential Manager");
    }

    function showCredentialResult(success, successMessage) {
        statusIsError = !success;
        statusMessage = success ? successMessage : (controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The credential operation failed."));
    }

    function performCredentialMigration() {
        showCredentialResult(controller.migrateCredentialStorage(credentialStorageToken(), removeCredentialSource.checked), qsTr("Credentials migrated and verified."));
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

    function accentToken() {
        return accentBox.currentIndex === 1 ? "system" : accentBox.currentIndex === 2 ? "custom" : "ztermy";
    }

    function previewDraft() {
        if (!visible || loadingDraft) {
            return;
        }
        const previewAccent = customAccentField.acceptableInput ? customAccentField.text : controller.customAccent;
        appearancePreviewRequested(themeToken(), opacitySlider.value, backdropToken(), accentToken(), previewAccent);
    }

    function selectCategory(category) {
        if (currentCategory === category) {
            focusCurrentCategory();
            return;
        }
        contentReveal = Theme.animationsEnabled ? 0.0 : 1.0;
        currentCategory = category;
        if (Theme.animationsEnabled) {
            categoryRevealAnimation.restart();
        }
    }

    function revealCurrentCategory() {
        contentReveal = Theme.animationsEnabled ? 0.0 : 1.0;
        if (Theme.animationsEnabled) {
            categoryRevealAnimation.restart();
        }
    }

    function focusCurrentCategory() {
        if (currentCategory === "application") {
            applicationCategory.focusAction();
        } else if (currentCategory === "terminal") {
            terminalCategory.focusAction();
        } else if (currentCategory === "shortcuts") {
            shortcutsCategory.focusAction();
        } else if (currentCategory === "security") {
            securityCategory.focusAction();
        } else {
            appearanceCategory.focusAction();
        }
    }

    function loadDraft() {
        loadingDraft = true;
        themeBox.currentIndex = themeIndex(controller.themePreference);
        opacitySlider.value = controller.backdropOpacity;
        backdropBox.currentIndex = backdropIndex(controller.backdropPreference);
        accentBox.currentIndex = accentIndex(controller.accentPreference);
        customAccentField.text = controller.customAccent;
        uiFontDraft = controller.uiFontFamily;
        terminalFontDraft = controller.terminalFontFamily;
        fontSizeBox.value = controller.terminalFontSize;
        showAllFontsSwitch.checked = controller.showAllTerminalFonts;
        ligatureSwitch.checked = controller.terminalLigatures;
        terminalOpacitySlider.value = controller.terminalBackgroundOpacity;
        cursorBox.currentIndex = cursorIndex(controller.cursorPreference);
        cursorBlinkSwitch.checked = controller.cursorBlink;
        copyOnSelectSwitch.checked = controller.copyOnSelect;
        multilinePasteSwitch.checked = controller.confirmMultilinePaste;
        languageDraft = controller.languagePreference;
        credentialStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        credentialCleanupStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        loadingDraft = false;
        previewDraft();
    }

    function applyDraft() {
        if (customAccentSelected && !customAccentField.acceptableInput) {
            statusIsError = true;
            statusMessage = qsTr("Custom accent must use the #RRGGBB format.");
            return;
        }
        const saved = controller.saveApplicationSettings(themeToken(), opacitySlider.value, backdropToken(), accentToken(), customAccentField.text, uiFontDraft, terminalFontDraft, fontSizeBox.value, showAllFontsSwitch.checked, ligatureSwitch.checked, terminalOpacitySlider.value, cursorToken(), cursorBlinkSwitch.checked, copyOnSelectSwitch.checked, multilinePasteSwitch.checked, languageDraft);
        statusIsError = !saved;
        statusMessage = saved ? qsTr("Settings saved and applied.") : qsTr("These settings could not be saved. Check the font and numeric ranges.");
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

    NumberAnimation {
        id: categoryRevealAnimation

        target: pane
        property: "contentReveal"
        from: 0.0
        to: 1.0
        duration: Theme.motionMedium
        easing.type: Easing.OutCubic
    }

    Rectangle {
        id: categoryRail

        objectName: "settingsCategoryRail"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: pane.compactLayout ? 132 : 196
        color: Theme.panelBackground

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.border
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: pane.compactLayout ? 10 : 14
            spacing: 8

            Text {
                Layout.leftMargin: 4
                Layout.bottomMargin: 8
                text: qsTr("SETTINGS")
                color: Theme.textSubtle
                font.family: Theme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.2
                font.weight: Font.DemiBold
            }

            CategoryButton {
                id: applicationCategory

                Layout.fillWidth: true
                title: qsTr("Application")
                iconName: "application"
                actionObjectName: "settingsApplicationCategory"
                selected: pane.currentCategory === "application"
                onActivated: pane.selectCategory("application")
            }

            CategoryButton {
                id: appearanceCategory

                Layout.fillWidth: true
                title: qsTr("Appearance")
                iconName: "appearance"
                actionObjectName: "settingsAppearanceCategory"
                selected: pane.currentCategory === "appearance"
                onActivated: pane.selectCategory("appearance")
            }

            CategoryButton {
                id: terminalCategory

                Layout.fillWidth: true
                title: qsTr("Terminal")
                iconName: "terminal"
                actionObjectName: "settingsTerminalCategory"
                selected: pane.currentCategory === "terminal"
                onActivated: pane.selectCategory("terminal")
            }

            CategoryButton {
                id: shortcutsCategory

                Layout.fillWidth: true
                title: qsTr("Shortcuts")
                iconName: "shortcuts"
                actionObjectName: "settingsShortcutsCategory"
                selected: pane.currentCategory === "shortcuts"
                onActivated: pane.selectCategory("shortcuts")
            }

            CategoryButton {
                id: securityCategory

                Layout.fillWidth: true
                title: qsTr("Security")
                iconName: "security"
                actionObjectName: "settingsSecurityCategory"
                selected: pane.currentCategory === "security"
                onActivated: pane.selectCategory("security")
            }

            Item {
                Layout.fillHeight: true
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                visible: !pane.compactLayout
                text: qsTr("Stored locally")
                color: Theme.textSubtle
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }
        }
    }

    ScrollView {
        id: scrollView

        anchors.left: categoryRail.right
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 8
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight + 72

        ColumnLayout {
            id: contentColumn

            x: Math.max(pane.contentInset, (scrollView.availableWidth - width) / 2)
            y: pane.compactLayout ? 24 : 38
            width: Math.max(0, Math.min(920, scrollView.availableWidth - (pane.contentInset * 2)))
            spacing: Theme.spacingSection
            opacity: pane.contentReveal

            Text {
                text: pane.currentCategory === "application" ? qsTr("Application") : pane.currentCategory === "appearance" ? qsTr("Appearance") : pane.currentCategory === "terminal" ? qsTr("Terminal") : pane.currentCategory === "shortcuts" ? qsTr("Shortcuts") : qsTr("Security")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                visible: pane.currentCategory !== "application"
                text: pane.currentCategory === "appearance" ? qsTr("Choose the language, interface font, theme, and Windows backdrop used across ztermy.") : pane.currentCategory === "terminal" ? qsTr("Configure the global terminal font, background, cursor, selection, and paste behavior.") : pane.currentCategory === "shortcuts" ? qsTr("Search, record, unbind, and reset keyboard shortcuts for registered ztermy actions.") : qsTr("Choose where SSH passwords and key passphrases are stored, unlock the portable vault, or migrate credentials safely.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            ShortcutSettings {
                id: shortcutSettings

                Layout.fillWidth: true
                visible: pane.currentCategory === "shortcuts"
                controller: pane.controller
                onVisibleChanged: {
                    if (!visible) {
                        finishRecording();
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "application"

                RowLayout {
                    objectName: "settingsApplicationBrandLockup"
                    Layout.fillWidth: true
                    Layout.minimumHeight: pane.compactLayout ? 116 : 188
                    spacing: pane.compactLayout ? 16 : 36
                    Accessible.name: qsTr("ztermy SSH Terminal")
                    Accessible.role: Accessible.Graphic

                    Image {
                        Layout.preferredWidth: pane.compactLayout ? 92 : 168
                        Layout.preferredHeight: pane.compactLayout ? 92 : 168
                        Layout.alignment: Qt.AlignVCenter
                        source: "image://ztermy-brand/app-icon/" + (Theme.dark ? "000001" : "000002") + "/" + (pane.compactLayout ? "compact" : "regular")
                        sourceSize.width: pane.compactLayout ? 184 : 336
                        sourceSize.height: pane.compactLayout ? 184 : 336
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        smooth: true
                        mipmap: true
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: pane.compactLayout ? 3 : 8

                        Row {
                            spacing: 0

                            Text {
                                text: qsTr("Z")
                                color: "#2AA8FF"
                                font.family: Theme.uiFont
                                font.pixelSize: pane.compactLayout ? 38 : 72
                                font.weight: Font.Bold
                            }

                            Text {
                                text: qsTr("termy")
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: pane.compactLayout ? 38 : 72
                                font.weight: Font.Bold
                            }
                        }

                        Text {
                            text: qsTr("SSH TERMINAL")
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: pane.compactLayout ? 11 : 18
                            font.letterSpacing: pane.compactLayout ? 3 : 6
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Windows 11 · Native Qt 6 · C++23")
                            color: Theme.textSubtle
                            wrapMode: Text.WordWrap
                            font.family: Theme.uiFont
                            font.pixelSize: pane.compactLayout ? Theme.textCompact : Theme.textLabel
                        }
                    }
                }
            }

            ReleaseIdentityCard {
                objectName: "settingsReleaseIdentityCard"
                Layout.fillWidth: true
                visible: pane.currentCategory === "application"
                compact: pane.compactLayout
                codename: "此"
                version: Qt.application.version
                verse: "天长地久有时尽，此恨绵绵无绝期。"
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "appearance"
                heading: qsTr("Language and interface")

                GridLayout {
                    objectName: "settingsApplicationGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: qsTr("Display language")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: languageBox

                        objectName: "settingsLanguage"
                        Layout.fillWidth: true
                        model: ["system", "en", "zh_CN"]
                        currentIndex: pane.languageIndex(pane.languageDraft)
                        accessibleName: qsTr("Application display language")
                        displayTextModel: [qsTr("System"), qsTr("English"), qsTr("Simplified Chinese")]
                        onActivated: index => pane.languageDraft = pane.languageToken(index)
                    }

                    Text {
                        Layout.columnSpan: parent.columns
                        Layout.fillWidth: true
                        text: qsTr("System follows the Windows display language. Unsupported system languages use English.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Label {
                        text: qsTr("Interface font")
                        color: Theme.text
                    }
                    FontPicker {
                        objectName: "settingsUiFont"
                        Layout.fillWidth: true
                        families: pane.uiFontOptions
                        family: pane.uiFontDraft
                        systemFamily: pane.fontCatalog.systemUiFamily
                        showFontPreview: false
                        accessibleName: qsTr("Application interface font")
                        searchObjectName: "settingsUiFontSearch"
                        onFamilyActivated: family => pane.uiFontDraft = family
                    }

                    Text {
                        Layout.columnSpan: parent.columns
                        Layout.fillWidth: true
                        text: pane.uiFontDraft.length === 0 ? qsTr("System default follows the Windows UI font and its script-aware fallback chain.") : pane.uiFontHasCjk ? qsTr("The selected font contains Chinese glyphs.") : qsTr("The selected font does not contain Chinese glyphs; Windows font fallback will render Chinese text.")
                        color: pane.uiFontDraft.length > 0 && !pane.uiFontHasCjk ? Theme.textMuted : Theme.textSubtle
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "appearance"
                heading: qsTr("Window appearance")

                GridLayout {
                    id: appearanceLayout

                    objectName: "settingsAppearanceGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: qsTr("Theme")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: themeBox
                        objectName: "settingsTheme"
                        Layout.fillWidth: true
                        model: ["system", "dark", "light"]
                        displayTextModel: [qsTr("System"), qsTr("Dark"), qsTr("Light")]
                        accessibleName: qsTr("Application theme")
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        text: qsTr("Accent color")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: accentBox
                        objectName: "settingsAccent"
                        Layout.fillWidth: true
                        model: ["ztermy", "system", "custom"]
                        displayTextModel: ["ztermy", qsTr("Follow Windows"), qsTr("Custom")]
                        accessibleName: qsTr("Application accent color source")
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        visible: pane.customAccentSelected
                        text: qsTr("Custom accent")
                        color: Theme.text
                    }
                    AppTextField {
                        id: customAccentField
                        objectName: "settingsCustomAccent"
                        visible: pane.customAccentSelected
                        Layout.fillWidth: true
                        placeholderText: "#22C55E"
                        selectByMouse: true
                        maximumLength: 7
                        validator: RegularExpressionValidator {
                            regularExpression: /^#[0-9A-Fa-f]{6}$/
                        }
                        Accessible.name: qsTr("Custom application accent color")
                        onTextChanged: pane.previewDraft()
                    }

                    Label {
                        text: qsTr("Windows backdrop")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: backdropBox
                        objectName: "settingsBackdrop"
                        Layout.fillWidth: true
                        model: ["acrylic", "transparent", "mica", "micaAlt"]
                        displayTextModel: [qsTr("Acrylic"), qsTr("Transparent"), "Mica", "Mica Alt"]
                        accessibleName: qsTr("Windows backdrop material")
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        visible: pane.adjustableBackdrop
                        text: qsTr("Window background opacity")
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
                            accessibleName: qsTr("Window background opacity")
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
                                    color: Theme.accent
                                }
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: pane.adjustableBackdrop ? qsTr("%1 · %2 · %3 · %4%").arg(themeBox.effectiveDisplayText).arg(accentBox.effectiveDisplayText).arg(backdropBox.effectiveDisplayText).arg(Math.round(opacitySlider.value * 100)) : qsTr("%1 · %2 · %3 · system controlled").arg(themeBox.effectiveDisplayText).arg(accentBox.effectiveDisplayText).arg(backdropBox.effectiveDisplayText)
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
                visible: pane.currentCategory === "terminal"
                heading: qsTr("Terminal")

                GridLayout {
                    id: terminalLayout

                    objectName: "settingsTerminalGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: qsTr("Font family")
                        color: Theme.text
                    }
                    FontPicker {
                        objectName: "settingsFontFamily"
                        Layout.fillWidth: true
                        families: pane.terminalFontOptions
                        family: pane.terminalFontDraft
                        systemFamily: pane.fontCatalog.systemUiFamily
                        accessibleName: qsTr("Terminal font family")
                        searchObjectName: "settingsTerminalFontSearch"
                        onFamilyActivated: family => pane.terminalFontDraft = family
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: showAllFontsSwitch.implicitHeight
                    }
                    AppCheckBox {
                        id: showAllFontsSwitch

                        objectName: "settingsShowAllTerminalFonts"
                        Layout.fillWidth: true
                        text: qsTr("Show all installed fonts")
                        accessibleName: text

                        AppToolTip {
                            delay: 500
                            text: qsTr("By default, only monospaced fonts suitable for a terminal grid are shown.")
                        }
                    }

                    Text {
                        Layout.columnSpan: terminalLayout.columns
                        Layout.fillWidth: true
                        visible: !pane.fontCatalog.isMonospaced(pane.terminalFontDraft)
                        text: qsTr("This is not a monospaced font. Terminal columns remain fixed, so some glyphs may overlap or leave extra spacing.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Label {
                        text: qsTr("Font size")
                        color: Theme.text
                    }
                    AppSpinBox {
                        id: fontSizeBox
                        objectName: "settingsFontSize"
                        Layout.fillWidth: true
                        from: 8
                        to: 32
                        editable: true
                        accessibleName: qsTr("Terminal font size")
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: ligatureSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: ligatureSwitch

                        objectName: "settingsTerminalLigatures"
                        Layout.fillWidth: true
                        enabled: pane.terminalLigatureAvailable
                        text: qsTr("Programming ligatures")
                        accessibleName: qsTr("Enable terminal programming ligatures")
                    }

                    Text {
                        Layout.columnSpan: terminalLayout.columns
                        Layout.fillWidth: true
                        text: pane.terminalLigatureAvailable ? qsTr("The selected font exposes OpenType ligature features. Ligatures are shaped only across compatible single-width terminal cells.") : qsTr("The selected font does not expose supported OpenType ligature features.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Label {
                        text: qsTr("Terminal background opacity")
                        color: Theme.text
                    }
                    RowLayout {
                        Layout.fillWidth: true

                        AppSlider {
                            id: terminalOpacitySlider
                            objectName: "settingsTerminalOpacity"
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            stepSize: 0.05
                            accessibleName: qsTr("Terminal background opacity")
                        }

                        Text {
                            Layout.preferredWidth: 42
                            horizontalAlignment: Text.AlignRight
                            text: Math.round(terminalOpacitySlider.value * 100) + "%"
                            color: Theme.textSoft
                            font.family: Theme.terminalFont
                            font.pixelSize: Theme.textLabel
                        }
                    }

                    Label {
                        text: qsTr("Cursor")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: cursorBox
                        objectName: "settingsCursor"
                        Layout.fillWidth: true
                        model: ["terminal", "block", "bar", "underline"]
                        displayTextModel: [qsTr("Terminal controlled"), qsTr("Block"), qsTr("Bar"), qsTr("Underline")]
                        accessibleName: qsTr("Terminal cursor style")
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: cursorBlinkSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: cursorBlinkSwitch
                        objectName: "settingsCursorBlink"
                        Layout.fillWidth: true
                        text: qsTr("Blink cursor")
                        accessibleName: qsTr("Blink terminal cursor")
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: copyOnSelectSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: copyOnSelectSwitch
                        objectName: "settingsCopyOnSelect"
                        Layout.fillWidth: true
                        text: qsTr("Copy selected terminal text automatically")
                        accessibleName: qsTr("Copy terminal selection automatically")
                    }

                    Item {
                        visible: !pane.compactLayout
                        implicitHeight: multilinePasteSwitch.implicitHeight
                    }
                    AppSwitch {
                        id: multilinePasteSwitch
                        objectName: "settingsMultilinePaste"
                        Layout.fillWidth: true
                        text: qsTr("Confirm before pasting multiple lines")
                        accessibleName: qsTr("Confirm multiline terminal paste")
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "security"
                heading: qsTr("Credential storage")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Windows Credential Manager is the installed-mode default. Portable mode uses an AES-256-GCM encrypted vault protected by your master password. Session storage is erased when ztermy exits.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 2
                        columnSpacing: 18
                        rowSpacing: 12

                        Label {
                            text: qsTr("Move credentials to")
                            color: Theme.text
                        }
                        AppComboBox {
                            id: credentialStorageBox

                            objectName: "settingsCredentialStorage"
                            Layout.fillWidth: true
                            model: ["system", "portable", "session"]
                            displayTextModel: [qsTr("Windows Credential Manager"), qsTr("Portable encrypted vault"), qsTr("Session only")]
                            accessibleName: qsTr("Credential storage destination")
                        }

                        Item {
                            visible: !pane.compactLayout
                            implicitHeight: removeCredentialSource.implicitHeight
                        }
                        AppCheckBox {
                            id: removeCredentialSource

                            objectName: "settingsCredentialRemoveSource"
                            Layout.fillWidth: true
                            checked: true
                            text: qsTr("Remove verified copies from the previous store")
                            accessibleName: text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Active store: %1").arg(pane.credentialStorageLabel(pane.controller.effectiveCredentialStorage))
                            color: Theme.textSoft
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }

                        ActionButton {
                            objectName: "settingsCredentialMigrate"
                            text: qsTr("Migrate")
                            accessibleName: qsTr("Migrate credentials to selected storage")
                            variant: "primary"
                            enabled: pane.credentialStorageToken() !== pane.controller.effectiveCredentialStorage && (pane.credentialStorageToken() !== "portable" || (pane.controller.portableVaultInitialized && !pane.controller.portableVaultLocked))
                            onClicked: {
                                if (removeCredentialSource.checked) {
                                    credentialMigrationDialog.openFrom(this);
                                } else {
                                    pane.performCredentialMigration();
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "security"
                heading: qsTr("Portable vault")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: !pane.controller.portableVaultInitialized ? qsTr("Create a master password before migrating credentials into the portable vault.") : pane.controller.portableVaultLocked ? qsTr("The portable vault is locked. Unlock it to connect with or modify saved credentials.") : qsTr("The portable vault is unlocked for this ztermy session. The master password is never persisted.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    AppTextField {
                        id: portablePasswordField

                        objectName: "settingsPortableVaultPassword"
                        Layout.fillWidth: true
                        placeholderText: pane.controller.portableVaultInitialized && pane.controller.portableVaultLocked ? qsTr("Master password (minimum 8 characters)") : pane.controller.portableVaultInitialized ? qsTr("New master password (minimum 8 characters)") : qsTr("Create master password (minimum 8 characters)")
                        echoMode: TextInput.Password
                        accessibleName: placeholderText
                        selectByMouse: true
                    }

                    AppTextField {
                        id: portablePasswordConfirmField

                        objectName: "settingsPortableVaultPasswordConfirm"
                        Layout.fillWidth: true
                        visible: !pane.controller.portableVaultInitialized || !pane.controller.portableVaultLocked
                        placeholderText: qsTr("Confirm master password (minimum 8 characters)")
                        echoMode: TextInput.Password
                        accessibleName: placeholderText
                        selectByMouse: true
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: portablePasswordField.text.length > 0 && portablePasswordField.text.length < 8
                        text: qsTr("The master password must contain at least 8 characters.")
                        color: Theme.dangerText
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ActionButton {
                            visible: pane.controller.portableVaultInitialized && !pane.controller.portableVaultLocked
                            text: qsTr("Lock")
                            accessibleName: qsTr("Lock portable credential vault")
                            onClicked: {
                                pane.controller.lockPortableCredentialVault();
                                portablePasswordField.text = "";
                                portablePasswordConfirmField.text = "";
                                pane.showCredentialResult(true, qsTr("Portable vault locked."));
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ActionButton {
                            objectName: "settingsPortableVaultAction"
                            text: !pane.controller.portableVaultInitialized ? qsTr("Create vault") : pane.controller.portableVaultLocked ? qsTr("Unlock") : qsTr("Change password")
                            accessibleName: qsTr("%1 for portable credential vault").arg(text)
                            variant: "primary"
                            enabled: portablePasswordField.text.length >= 8 && (pane.controller.portableVaultInitialized && pane.controller.portableVaultLocked || portablePasswordField.text === portablePasswordConfirmField.text)
                            onClicked: {
                                let success = false;
                                let message = "";
                                if (!pane.controller.portableVaultInitialized) {
                                    success = pane.controller.initializePortableCredentialVault(portablePasswordField.text);
                                    message = qsTr("Portable vault created and unlocked.");
                                } else if (pane.controller.portableVaultLocked) {
                                    success = pane.controller.unlockPortableCredentialVault(portablePasswordField.text);
                                    message = qsTr("Portable vault unlocked.");
                                } else {
                                    success = pane.controller.changePortableVaultMasterPassword(portablePasswordField.text);
                                    message = qsTr("Portable vault password changed.");
                                }
                                pane.showCredentialResult(success, message);
                                if (success) {
                                    portablePasswordField.text = "";
                                    portablePasswordConfirmField.text = "";
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "security"
                heading: qsTr("Credential cleanup")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Clear active credentials or remove copies deliberately retained in another store. Clearing the active store also detaches credentials from saved hosts.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 3
                        columnSpacing: Theme.spacingControl
                        rowSpacing: Theme.spacingControl

                        Label {
                            text: qsTr("Credential store")
                            color: Theme.text
                        }

                        AppComboBox {
                            id: credentialCleanupStorageBox

                            objectName: "settingsCredentialCleanupStorage"
                            Layout.fillWidth: true
                            model: ["system", "portable", "session"]
                            displayTextModel: [qsTr("Windows Credential Manager"), qsTr("Portable encrypted vault"), qsTr("Session only")]
                            accessibleName: qsTr("Credential store to clear")
                        }

                        ActionButton {
                            id: removeAllCredentialsButton

                            objectName: "settingsRemoveAllCredentials"
                            Layout.fillWidth: pane.compactLayout
                            text: qsTr("Clear store")
                            accessibleName: qsTr("Clear selected credential store")
                            onClicked: removeAllCredentialsDialog.openFrom(removeAllCredentialsButton)
                        }
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
                visible: pane.currentCategory === "appearance" || pane.currentCategory === "terminal"
                columns: pane.compactLayout ? 1 : 4
                columnSpacing: Theme.spacingControl
                rowSpacing: Theme.spacingControl

                ActionButton {
                    objectName: "settingsReset"
                    Layout.fillWidth: pane.compactLayout
                    text: qsTr("Reset defaults")
                    accessibleName: qsTr("Reset all application settings")
                    onClicked: {
                        const reset = pane.controller.resetApplicationSettings();
                        pane.statusIsError = !reset;
                        pane.statusMessage = reset ? qsTr("Default settings restored.") : qsTr("Default settings could not be restored.");
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
                    text: qsTr("Discard changes")
                    accessibleName: qsTr("Discard unsaved setting changes")
                    onClicked: {
                        pane.loadDraft();
                        pane.statusIsError = false;
                        pane.statusMessage = qsTr("Unsaved changes discarded.");
                    }
                }

                ActionButton {
                    id: applyButton
                    objectName: "settingsApply"
                    Layout.fillWidth: pane.compactLayout
                    text: qsTr("Apply")
                    accessibleName: qsTr("Apply application settings")
                    variant: "primary"
                    enabled: !pane.customAccentSelected || customAccentField.acceptableInput
                    onClicked: pane.applyDraft()
                }
            }
        }
    }

    ConfirmationDialog {
        id: credentialMigrationDialog

        heading: pane.credentialStorageToken() === "session" ? qsTr("Move credentials to session-only storage?") : qsTr("Remove credentials from the previous store?")
        description: pane.credentialStorageToken() === "session" ? qsTr("Credentials will be verified in memory and removed from the persistent store. They will be lost when ztermy exits.") : qsTr("After every credential is copied and verified, ztermy will remove its copy from the previous store.")
        acceptText: qsTr("Migrate and remove")
        destructive: pane.credentialStorageToken() === "session"
        onAccepted: pane.performCredentialMigration()
    }

    ConfirmationDialog {
        id: removeAllCredentialsDialog

        readonly property string selectedStorage: pane.credentialStorageTokenForIndex(credentialCleanupStorageBox.currentIndex)
        readonly property bool clearsActiveStorage: selectedStorage === pane.controller.effectiveCredentialStorage

        heading: clearsActiveStorage ? qsTr("Clear the active credential store?") : qsTr("Clear retained credential copies?")
        description: clearsActiveStorage ? qsTr("This permanently removes ztermy passwords and key passphrases from the active store. Host profiles remain, but will ask for credentials next time.") : qsTr("This permanently removes all ztermy credential copies from the selected inactive store. Credentials and host references in the active store remain unchanged.")
        acceptText: qsTr("Clear store")
        destructive: true
        onAccepted: {
            pane.showCredentialResult(pane.controller.clearCredentialStorage(selectedStorage), clearsActiveStorage ? qsTr("Active credentials were removed and detached from saved hosts.") : qsTr("Retained credential copies were removed from the selected store."));
            focusRestoreItem = removeAllCredentialsButton;
        }
    }
}
