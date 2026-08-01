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
    property string currentCategory: "appearance"
    property real contentReveal: 1.0
    readonly property bool draftDark: themeBox.currentIndex === 1 || (themeBox.currentIndex === 0 && Theme.systemDark)
    readonly property bool adjustableBackdrop: backdropBox.currentIndex === 0 || backdropBox.currentIndex === 1
    readonly property bool customAccentSelected: accentBox.currentIndex === 2
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset

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
            accessibleName: categoryControl.title + " settings"
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

    function credentialStorageToken() {
        return credentialStorageTokenForIndex(credentialStorageBox.currentIndex);
    }

    function credentialStorageTokenForIndex(index) {
        return index === 1 ? "portable" : index === 2 ? "session" : "system";
    }

    function showCredentialResult(success, successMessage) {
        statusIsError = !success;
        statusMessage = success ? successMessage : (controller.credentialOperationError.length > 0 ? controller.credentialOperationError : "The credential operation failed.");
    }

    function performCredentialMigration() {
        showCredentialResult(controller.migrateCredentialStorage(credentialStorageToken(), removeCredentialSource.checked), "Credentials migrated and verified.");
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
        if (currentCategory === "terminal") {
            terminalCategory.focusAction();
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
        fontFamilyField.text = controller.terminalFontFamily;
        fontSizeBox.value = controller.terminalFontSize;
        terminalOpacitySlider.value = controller.terminalBackgroundOpacity;
        cursorBox.currentIndex = cursorIndex(controller.cursorPreference);
        cursorBlinkSwitch.checked = controller.cursorBlink;
        copyOnSelectSwitch.checked = controller.copyOnSelect;
        multilinePasteSwitch.checked = controller.confirmMultilinePaste;
        credentialStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        credentialCleanupStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        loadingDraft = false;
        previewDraft();
    }

    function applyDraft() {
        if (customAccentSelected && !customAccentField.acceptableInput) {
            statusIsError = true;
            statusMessage = "Custom accent must use the #RRGGBB format.";
            return;
        }
        const saved = controller.saveApplicationSettings(themeToken(), opacitySlider.value, backdropToken(), accentToken(), customAccentField.text, fontFamilyField.text, fontSizeBox.value, terminalOpacitySlider.value, cursorToken(), cursorBlinkSwitch.checked, copyOnSelectSwitch.checked, multilinePasteSwitch.checked);
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
                text: "SETTINGS"
                color: Theme.textSubtle
                font.family: Theme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.2
                font.weight: Font.DemiBold
            }

            CategoryButton {
                id: appearanceCategory

                Layout.fillWidth: true
                title: "Appearance"
                iconName: "appearance"
                actionObjectName: "settingsAppearanceCategory"
                selected: pane.currentCategory === "appearance"
                onActivated: pane.selectCategory("appearance")
            }

            CategoryButton {
                id: securityCategory

                Layout.fillWidth: true
                title: "Security"
                iconName: "settings"
                actionObjectName: "settingsSecurityCategory"
                selected: pane.currentCategory === "security"
                onActivated: pane.selectCategory("security")
            }

            CategoryButton {
                id: terminalCategory

                Layout.fillWidth: true
                title: "Terminal"
                iconName: "terminal"
                actionObjectName: "settingsTerminalCategory"
                selected: pane.currentCategory === "terminal"
                onActivated: pane.selectCategory("terminal")
            }

            Item {
                Layout.fillHeight: true
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                visible: !pane.compactLayout
                text: "Stored locally"
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
                text: pane.currentCategory === "appearance" ? "Appearance" : pane.currentCategory === "terminal" ? "Terminal" : "Security"
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: pane.currentCategory === "appearance" ? "Choose the application theme and Windows backdrop used across ztermy." : pane.currentCategory === "terminal" ? "Configure the global terminal font, background, cursor, selection, and paste behavior." : "Choose where SSH passwords and key passphrases are stored, unlock the portable vault, or migrate credentials safely."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "appearance"
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
                        text: "Accent color"
                        color: Theme.text
                    }
                    AppComboBox {
                        id: accentBox
                        objectName: "settingsAccent"
                        Layout.fillWidth: true
                        model: ["ztermy", "Follow Windows", "Custom"]
                        accessibleName: "Application accent color source"
                        onCurrentIndexChanged: pane.previewDraft()
                    }

                    Label {
                        visible: pane.customAccentSelected
                        text: "Custom accent"
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
                        Accessible.name: "Custom application accent color"
                        onTextChanged: pane.previewDraft()
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
                        text: "Window background opacity"
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
                                    color: Theme.accent
                                }
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: themeBox.currentText + " · " + accentBox.currentText + " · " + backdropBox.currentText + (pane.adjustableBackdrop ? " · " + Math.round(opacitySlider.value * 100) + "%" : " · system controlled")
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
                        text: "Terminal background opacity"
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
                            accessibleName: "Terminal background opacity"
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

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "security"
                heading: "Credential storage"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: "Windows Credential Manager is the installed-mode default. Portable mode uses an AES-256-GCM encrypted vault protected by your master password. Session storage is erased when ztermy exits."
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
                            text: "Move credentials to"
                            color: Theme.text
                        }
                        AppComboBox {
                            id: credentialStorageBox

                            objectName: "settingsCredentialStorage"
                            Layout.fillWidth: true
                            model: ["Windows Credential Manager", "Portable encrypted vault", "Session only"]
                            accessibleName: "Credential storage destination"
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
                            text: "Remove verified copies from the previous store"
                            accessibleName: text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: "Active store: " + pane.controller.effectiveCredentialStorage
                            color: Theme.textSoft
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }

                        ActionButton {
                            objectName: "settingsCredentialMigrate"
                            text: "Migrate"
                            accessibleName: "Migrate credentials to selected storage"
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
                heading: "Portable vault"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: !pane.controller.portableVaultInitialized ? "Create a master password before migrating credentials into the portable vault." : pane.controller.portableVaultLocked ? "The portable vault is locked. Unlock it to connect with or modify saved credentials." : "The portable vault is unlocked for this ztermy session. The master password is never persisted."
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    AppTextField {
                        id: portablePasswordField

                        objectName: "settingsPortableVaultPassword"
                        Layout.fillWidth: true
                        placeholderText: pane.controller.portableVaultInitialized && pane.controller.portableVaultLocked ? "Master password (minimum 8 characters)" : pane.controller.portableVaultInitialized ? "New master password (minimum 8 characters)" : "Create master password (minimum 8 characters)"
                        echoMode: TextInput.Password
                        accessibleName: placeholderText
                        selectByMouse: true
                    }

                    AppTextField {
                        id: portablePasswordConfirmField

                        objectName: "settingsPortableVaultPasswordConfirm"
                        Layout.fillWidth: true
                        visible: !pane.controller.portableVaultInitialized || !pane.controller.portableVaultLocked
                        placeholderText: "Confirm master password (minimum 8 characters)"
                        echoMode: TextInput.Password
                        accessibleName: placeholderText
                        selectByMouse: true
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: portablePasswordField.text.length > 0 && portablePasswordField.text.length < 8
                        text: "The master password must contain at least 8 characters."
                        color: Theme.dangerText
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ActionButton {
                            visible: pane.controller.portableVaultInitialized && !pane.controller.portableVaultLocked
                            text: "Lock"
                            accessibleName: "Lock portable credential vault"
                            onClicked: {
                                pane.controller.lockPortableCredentialVault();
                                portablePasswordField.text = "";
                                portablePasswordConfirmField.text = "";
                                pane.showCredentialResult(true, "Portable vault locked.");
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ActionButton {
                            objectName: "settingsPortableVaultAction"
                            text: !pane.controller.portableVaultInitialized ? "Create vault" : pane.controller.portableVaultLocked ? "Unlock" : "Change password"
                            accessibleName: text + " for portable credential vault"
                            variant: "primary"
                            enabled: portablePasswordField.text.length >= 8 && (pane.controller.portableVaultInitialized && pane.controller.portableVaultLocked || portablePasswordField.text === portablePasswordConfirmField.text)
                            onClicked: {
                                let success = false;
                                let message = "";
                                if (!pane.controller.portableVaultInitialized) {
                                    success = pane.controller.initializePortableCredentialVault(portablePasswordField.text);
                                    message = "Portable vault created and unlocked.";
                                } else if (pane.controller.portableVaultLocked) {
                                    success = pane.controller.unlockPortableCredentialVault(portablePasswordField.text);
                                    message = "Portable vault unlocked.";
                                } else {
                                    success = pane.controller.changePortableVaultMasterPassword(portablePasswordField.text);
                                    message = "Portable vault password changed.";
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
                heading: "Credential cleanup"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: "Clear active credentials or remove copies deliberately retained in another store. Clearing the active store also detaches credentials from saved hosts."
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
                            text: "Credential store"
                            color: Theme.text
                        }

                        AppComboBox {
                            id: credentialCleanupStorageBox

                            objectName: "settingsCredentialCleanupStorage"
                            Layout.fillWidth: true
                            model: ["Windows Credential Manager", "Portable encrypted vault", "Session only"]
                            accessibleName: "Credential store to clear"
                        }

                        ActionButton {
                            id: removeAllCredentialsButton

                            objectName: "settingsRemoveAllCredentials"
                            Layout.fillWidth: pane.compactLayout
                            text: "Clear store"
                            accessibleName: "Clear selected credential store"
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
                visible: pane.currentCategory !== "security"
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
                    id: applyButton
                    objectName: "settingsApply"
                    Layout.fillWidth: pane.compactLayout
                    text: "Apply"
                    accessibleName: "Apply application settings"
                    variant: "primary"
                    enabled: !pane.customAccentSelected || customAccentField.acceptableInput
                    onClicked: pane.applyDraft()
                }
            }
        }
    }

    ConfirmationDialog {
        id: credentialMigrationDialog

        heading: pane.credentialStorageToken() === "session" ? "Move credentials to session-only storage?" : "Remove credentials from the previous store?"
        description: pane.credentialStorageToken() === "session" ? "Credentials will be verified in memory and removed from the persistent store. They will be lost when ztermy exits." : "After every credential is copied and verified, ztermy will remove its copy from the previous store."
        acceptText: "Migrate and remove"
        destructive: pane.credentialStorageToken() === "session"
        onAccepted: pane.performCredentialMigration()
    }

    ConfirmationDialog {
        id: removeAllCredentialsDialog

        readonly property string selectedStorage: pane.credentialStorageTokenForIndex(credentialCleanupStorageBox.currentIndex)
        readonly property bool clearsActiveStorage: selectedStorage === pane.controller.effectiveCredentialStorage

        heading: clearsActiveStorage ? "Clear the active credential store?" : "Clear retained credential copies?"
        description: clearsActiveStorage ? "This permanently removes ztermy passwords and key passphrases from the active store. Host profiles remain, but will ask for credentials next time." : "This permanently removes all ztermy credential copies from the selected inactive store. Credentials and host references in the active store remain unchanged."
        acceptText: "Clear store"
        destructive: true
        onAccepted: {
            pane.showCredentialResult(pane.controller.clearCredentialStorage(selectedStorage), clearsActiveStorage ? "Active credentials were removed and detached from saved hosts." : "Retained credential copies were removed from the selected store.");
            focusRestoreItem = removeAllCredentialsButton;
        }
    }
}
