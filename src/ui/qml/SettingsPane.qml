pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "settingsPane"
    required property var controller
    required property var diagnostics
    required property var fontCatalog
    required property var windowChrome
    property bool loadingDraft: false
    property string statusMessage: ""
    property bool statusIsError: false
    property bool statusVisible: false
    property string currentCategory: "application"
    property string languageDraft: "system"
    property string uiFontDraft: ""
    property string terminalFontDraft: "Cascadia Mono"
    property string aiBaseUrlDraft: "https://api.openai.com"
    property string aiModelDraft: ""
    property string aiSelectedProviderToken: "openai-responses"
    property string aiReasoningDraft: "auto"
    property string aiProxyModeDraft: "system"
    property string aiProxyUrlDraft: ""
    property string aiProxyUsernameDraft: ""
    property string aiQuickMessageEditingId: ""
    property string aiQuickMessageNameDraft: ""
    property string aiQuickMessageSlugDraft: ""
    property string aiQuickMessageDescriptionDraft: ""
    property string aiQuickMessageContentDraft: ""
    property bool aiQuickMessageSlugManual: false
    property bool performanceModeDraft: false
    readonly property var aiProviderTokens: ["openai-responses", "openai-chatgpt", "anthropic", "gemini", "openrouter", "deepseek", "kimi", "qwen", "zai", "ollama", "openai-compatible"]
    readonly property var aiReasoningOptions: controller.aiReasoningCapabilities(aiProviderToken(), aiModelDraft)
    property string mcpOriginalId: ""
    property string mcpEditingId: ""
    property string mcpNamespaceDraft: ""
    property string mcpProgramDraft: ""
    property string mcpArgumentsDraft: "[]"
    property string mcpWorkingDirectoryDraft: ""
    property var mcpReviewTool: null
    property real contentReveal: 1.0
    readonly property bool shortcutRecording: shortcutSettings.recording
    readonly property bool draftDark: themeBox.currentIndex === 1 || (themeBox.currentIndex === 0 && Theme.systemDark)
    readonly property bool adjustableBackdrop: backdropBox.currentIndex === 0 || backdropBox.currentIndex === 1
    readonly property bool solidBackdrop: backdropBox.currentIndex === 4
    readonly property bool customAccentSelected: accentBox.currentIndex === 2
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? 10 : 16
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

        implicitHeight: 36
        radius: Theme.radiusControl
        color: selected ? Theme.controlBackground : (categoryAction.hovered || categoryAction.visualFocus ? Theme.controlHover : "transparent")
        border.color: categoryAction.visualFocus ? Theme.focus : "transparent"
        border.width: categoryAction.visualFocus ? 1 : 0

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
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

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
                font.pixelSize: Theme.textLabel
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
        return token === "transparent" ? 1 : token === "mica" ? 2 : token === "micaAlt" ? 3 : token === "solid" ? 4 : 0;
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

    function aiProviderIndex(token) {
        const index = aiProviderTokens.indexOf(token);
        return index < 0 ? 0 : index;
    }

    function aiProviderToken() {
        return aiProviderTokens[Math.max(0, aiProviderBox.currentIndex)];
    }

    function aiProviderDefault(token) {
        if (token === "openai-chatgpt")
            return "https://chatgpt.com/backend-api/codex";
        if (token === "anthropic")
            return "https://api.anthropic.com";
        if (token === "deepseek")
            return "https://api.deepseek.com";
        if (token === "kimi")
            return "https://api.moonshot.ai";
        if (token === "zai")
            return "https://api.z.ai/api/paas/v4";
        if (token === "gemini")
            return "https://generativelanguage.googleapis.com/v1beta/openai/";
        if (token === "openrouter")
            return "https://openrouter.ai/api/v1";
        if (token === "qwen")
            return "https://dashscope.aliyuncs.com/compatible-mode/v1";
        if (token === "ollama")
            return "http://127.0.0.1:11434";
        if (token === "openai-compatible")
            return "";
        return "https://api.openai.com";
    }

    function selectAiProvider(index) {
        const previousDefault = aiProviderDefault(aiSelectedProviderToken);
        const nextToken = aiProviderTokens[index];
        if (aiBaseUrlDraft.trim().length === 0 || aiBaseUrlDraft === previousDefault) {
            aiBaseUrlDraft = aiProviderDefault(nextToken);
        }
        aiSelectedProviderToken = nextToken;
        aiModelDraft = "";
    }

    function aiPermissionIndex(token) {
        return token === "read-only" ? 0 : token === "auto" ? 2 : token === "yolo" ? 3 : 1;
    }

    function aiPermissionToken() {
        return ["read-only", "ask", "auto", "yolo"][aiPermissionBox.currentIndex];
    }

    function aiReasoningIndex(token) {
        const index = aiReasoningOptions.tokens.indexOf(token);
        return index < 0 ? 0 : index;
    }

    function aiReasoningToken() {
        const tokens = aiReasoningOptions.tokens;
        return tokens.length > 0 ? tokens[Math.max(0, aiReasoningBox.currentIndex)] : "auto";
    }

    function aiProxyIndex(token) {
        return token === "direct" ? 1 : token === "custom" ? 2 : 0;
    }

    function aiProxyToken() {
        return ["system", "direct", "custom"][aiProxyBox.currentIndex];
    }

    function aiSubscriptionWindowLabel(window) {
        if (!window || window.remainingPercent === undefined)
            return "";
        const duration = Number(window.durationSeconds || 0);
        const period = duration >= 604800 ? qsTr("weekly") : duration >= 3600 ? qsTr("%1 h").arg(Math.round(duration / 3600)) : qsTr("current window");
        return qsTr("%1 · %2% left").arg(period).arg(Math.round(Number(window.remainingPercent)));
    }

    function aiSubscriptionUsageSummary() {
        const usage = controller.aiChatGptUsage || ({});
        if (usage.state === "loading")
            return qsTr("Checking Codex allowance…");
        if (usage.state === "error")
            return usage.error || qsTr("Codex allowance is unavailable.");
        const limits = usage.limits || [];
        if (usage.state !== "ready" || limits.length === 0)
            return qsTr("Codex allowance has not been checked yet.");
        const plan = usage.planType ? String(usage.planType).toUpperCase() : qsTr("ChatGPT plan");
        const primary = aiSubscriptionWindowLabel(limits[0].primary);
        const secondary = aiSubscriptionWindowLabel(limits[0].secondary);
        return [plan, primary, secondary].filter(value => value.length > 0).join(" · ");
    }

    function aiRuleMatcherIndex(token) {
        return token === "prefix" ? 1 : token === "glob" ? 2 : token === "regex" ? 3 : token === "all" ? 4 : 0;
    }

    function aiRuleCapabilityLabel(token) {
        if (token === "pty-input")
            return qsTr("Terminal input");
        if (token === "terminal-interrupt")
            return qsTr("Terminal interrupt");
        if (token === "runbook")
            return qsTr("Runbook changes");
        if (token === "sftp-download")
            return qsTr("SFTP download");
        if (token === "sftp-upload")
            return qsTr("SFTP upload");
        if (token === "mcp-tool")
            return qsTr("MCP tool");
        return qsTr("Terminal command");
    }

    function aiRuleScopeLabel(rule) {
        if (rule.duration === "profile")
            return rule.profileName.length > 0 ? qsTr("Profile · %1").arg(rule.profileName) : qsTr("Profile");
        if (rule.duration === "global")
            return qsTr("All Profiles");
        return qsTr("This session");
    }

    function normalizeAiQuickMessageSlug(value) {
        return value.trim().toLocaleLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 48);
    }

    function aiUserSkillCount(ready) {
        const skills = pane.controller.aiUserSkills || [];
        let count = 0;
        for (let index = 0; index < skills.length; ++index) {
            if (skills[index].ready === ready)
                ++count;
        }
        return count;
    }

    function resetAiQuickMessageDraft() {
        aiQuickMessageEditingId = "";
        aiQuickMessageNameDraft = "";
        aiQuickMessageSlugDraft = "";
        aiQuickMessageDescriptionDraft = "";
        aiQuickMessageContentDraft = "";
        aiQuickMessageSlugManual = false;
    }

    function editAiQuickMessage(message) {
        aiQuickMessageEditingId = message.id;
        aiQuickMessageNameDraft = message.name;
        aiQuickMessageSlugDraft = message.slug;
        aiQuickMessageDescriptionDraft = message.description || "";
        aiQuickMessageContentDraft = message.content;
        aiQuickMessageSlugManual = true;
    }

    function saveAiQuickMessageDraft() {
        const saved = controller.saveAiQuickMessage(aiQuickMessageEditingId, aiQuickMessageNameDraft, aiQuickMessageSlugDraft, aiQuickMessageContentDraft, aiQuickMessageDescriptionDraft);
        presentStatus(saved ? qsTr("Quick message saved.") : (controller.aiQuickMessageError.length > 0 ? controller.aiQuickMessageError : qsTr("The quick message is invalid.")), !saved, saved);
        if (saved)
            resetAiQuickMessageDraft();
    }

    function resetMcpDraft() {
        mcpOriginalId = "";
        mcpEditingId = "";
        mcpNamespaceDraft = "";
        mcpProgramDraft = "";
        mcpArgumentsDraft = "[]";
        mcpWorkingDirectoryDraft = "";
        mcpTrustBox.currentIndex = 1;
        mcpEnabledSwitch.checked = true;
    }

    function editMcpServer(server) {
        mcpOriginalId = server.id;
        mcpEditingId = server.id;
        mcpNamespaceDraft = server.namespace;
        mcpProgramDraft = server.program;
        mcpArgumentsDraft = JSON.stringify(server.arguments);
        mcpWorkingDirectoryDraft = server.workingDirectory;
        mcpTrustBox.currentIndex = server.trust === "execute" ? 2 : server.trust === "observe" ? 1 : 0;
        mcpEnabledSwitch.checked = server.enabled;
    }

    function saveMcpDraft() {
        let argumentsValue;
        try {
            argumentsValue = JSON.parse(mcpArgumentsDraft);
        } catch (error) {
            presentStatus(qsTr("MCP arguments must be a JSON string array."), true, false);
            return;
        }
        if (!Array.isArray(argumentsValue) || argumentsValue.some(value => typeof value !== "string")) {
            presentStatus(qsTr("MCP arguments must be a JSON string array."), true, false);
            return;
        }
        const trust = ["disabled", "observe", "execute"][mcpTrustBox.currentIndex];
        const saved = controller.saveMcpServer(mcpEditingId, mcpNamespaceDraft, mcpProgramDraft, argumentsValue, mcpWorkingDirectoryDraft, trust, mcpEnabledSwitch.checked);
        presentStatus(saved ? qsTr("MCP server saved. Review each discovered tool before enabling it for AI.") : (controller.mcpOperationError.length > 0 ? controller.mcpOperationError : qsTr("The MCP server configuration is invalid.")), !saved, saved);
        if (saved) {
            resetMcpDraft();
        }
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
        presentStatus(success ? successMessage : (controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The credential operation failed.")), !success, success);
    }

    function presentStatus(message, isError, dismissAutomatically) {
        statusDismissTimer.stop();
        statusClearTimer.stop();
        statusMessage = message;
        statusIsError = isError;
        statusVisible = message.length > 0;
        if (statusVisible && dismissAutomatically) {
            statusDismissTimer.restart();
        }
    }

    function performCredentialMigration() {
        showCredentialResult(controller.migrateCredentialStorage(credentialStorageToken(), removeCredentialSource.checked), qsTr("Credentials migrated and verified."));
    }

    function themeToken() {
        return themeBox.currentIndex === 0 ? "system" : themeBox.currentIndex === 2 ? "light" : "dark";
    }

    function backdropToken() {
        return backdropBox.currentIndex === 1 ? "transparent" : backdropBox.currentIndex === 2 ? "mica" : backdropBox.currentIndex === 3 ? "micaAlt" : backdropBox.currentIndex === 4 ? "solid" : "acrylic";
    }

    function cursorToken() {
        return cursorBox.currentIndex === 1 ? "block" : cursorBox.currentIndex === 2 ? "bar" : cursorBox.currentIndex === 3 ? "underline" : "terminal";
    }

    function rightClickIndex(token) {
        return token === "copy-paste" ? 1 : token === "paste" ? 2 : token === "select-word" ? 3 : 0;
    }

    function rightClickToken() {
        return rightClickBox.currentIndex === 1 ? "copy-paste" : rightClickBox.currentIndex === 2 ? "paste" : rightClickBox.currentIndex === 3 ? "select-word" : "context-menu";
    }

    function middleClickIndex(token) {
        return token === "paste" ? 1 : token === "context-menu" ? 2 : 0;
    }

    function middleClickToken() {
        return middleClickBox.currentIndex === 1 ? "paste" : middleClickBox.currentIndex === 2 ? "context-menu" : "disabled";
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
        } else if (currentCategory === "about") {
            aboutCategory.focusAction();
        } else if (currentCategory === "terminal") {
            terminalCategory.focusAction();
        } else if (currentCategory === "shortcuts") {
            shortcutsCategory.focusAction();
        } else if (currentCategory === "sftp") {
            sftpCategory.focusAction();
        } else if (currentCategory === "ai") {
            aiCategory.focusAction();
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
        keepSelectionAfterCopySwitch.checked = controller.keepSelectionAfterCopy;
        multilinePasteSwitch.checked = controller.confirmMultilinePaste;
        rightClickBox.currentIndex = rightClickIndex(controller.terminalRightClickBehavior);
        middleClickBox.currentIndex = middleClickIndex(controller.terminalMiddleClickBehavior);
        wordDelimitersField.text = controller.terminalWordDelimiters;
        wheelRowsBox.value = controller.terminalScrollRows;
        sftpShowHiddenSwitch.checked = controller.sftpShowHiddenFiles;
        sftpConfirmDeleteSwitch.checked = controller.sftpConfirmDelete;
        closeToTraySwitch.checked = controller.closeToTray;
        performanceModeDraft = controller.performanceMode;
        performanceModeSwitch.checked = performanceModeDraft;
        languageDraft = controller.languagePreference;
        credentialStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        credentialCleanupStorageBox.currentIndex = credentialStorageIndex(controller.effectiveCredentialStorage);
        aiSelectedProviderToken = controller.aiProviderPreference;
        aiProviderBox.currentIndex = aiProviderIndex(aiSelectedProviderToken);
        aiPermissionBox.currentIndex = aiPermissionIndex(controller.aiPermissionPreference);
        aiBaseUrlDraft = controller.aiBaseUrl;
        aiModelDraft = controller.aiModel;
        aiReasoningDraft = controller.aiReasoningPreference;
        aiReasoningBox.currentIndex = aiReasoningIndex(aiReasoningDraft);
        aiProxyModeDraft = controller.aiProxyPreference;
        aiProxyBox.currentIndex = aiProxyIndex(aiProxyModeDraft);
        aiProxyUrlDraft = controller.aiProxyUrl;
        aiProxyUsernameDraft = controller.aiProxyUsername;
        aiProxyPasswordField.text = "";
        aiAutomaticContextSwitch.checked = controller.aiAutomaticContext;
        aiDebugTraceSwitch.checked = controller.aiDebugTraceEnabled;
        aiConversationHistorySwitch.checked = controller.aiConversationHistoryEnabled;
        aiApiKeyField.text = "";
        loadingDraft = false;
        previewDraft();
    }

    function applyDraft() {
        if (customAccentSelected && !customAccentField.acceptableInput) {
            presentStatus(qsTr("Custom accent must use the #RRGGBB format."), true, false);
            return;
        }
        const wantsOpaqueSurface = performanceModeDraft || backdropToken() === "solid";
        const restartRequired = wantsOpaqueSurface !== windowChrome.opaqueSurface || performanceModeDraft !== windowChrome.performanceModeActive;
        const saved = controller.saveApplicationSettings(themeToken(), opacitySlider.value, backdropToken(), accentToken(), customAccentField.text, uiFontDraft, terminalFontDraft, fontSizeBox.value, showAllFontsSwitch.checked, ligatureSwitch.checked, terminalOpacitySlider.value, cursorToken(), cursorBlinkSwitch.checked, copyOnSelectSwitch.checked, keepSelectionAfterCopySwitch.checked, multilinePasteSwitch.checked, languageDraft, sftpShowHiddenSwitch.checked, sftpConfirmDeleteSwitch.checked, closeToTraySwitch.checked, performanceModeDraft, rightClickToken(), middleClickToken(), wordDelimitersField.text, wheelRowsBox.value);
        presentStatus(saved ? restartRequired ? qsTr("Settings saved. Restart ztermy to apply the rendering mode.") : qsTr("Settings saved and applied.") : qsTr("These settings could not be saved. Check the font and numeric ranges."), !saved, saved);
        if (!saved) {
            loadDraft();
        }
    }

    Connections {
        target: pane.controller

        function onAiModelsChanged() {
            if (!pane.controller.aiModelsLoading && pane.controller.aiModelsError.length === 0 && pane.aiModelDraft.length === 0 && pane.controller.aiAvailableModels.length > 0) {
                pane.aiModelDraft = pane.controller.aiAvailableModels[0];
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            loadDraft();
        } else {
            appearancePreviewEnded();
        }
    }
    onCurrentCategoryChanged: {
        if (currentCategory === "ai")
            controller.ensureAiUserSkillsLoaded();
    }

    Component.onCompleted: {
        loadDraft();
        if (currentCategory === "ai")
            controller.ensureAiUserSkillsLoaded();
    }

    NumberAnimation {
        id: categoryRevealAnimation

        target: pane
        property: "contentReveal"
        from: 0.0
        to: 1.0
        duration: Theme.motionMedium
        easing.type: Easing.OutCubic
    }

    Timer {
        id: statusDismissTimer

        interval: 3600
        repeat: false
        onTriggered: {
            pane.statusVisible = false;
            statusClearTimer.restart();
        }
    }

    Timer {
        id: statusClearTimer

        interval: Theme.animationsEnabled ? Theme.motionMedium : 0
        repeat: false
        onTriggered: pane.statusMessage = ""
    }

    Rectangle {
        id: categoryRail

        objectName: "settingsCategoryRail"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: pane.compactLayout ? 140 : 208
        color: Theme.panelBackground

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.border
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: pane.compactLayout ? 8 : 10
            spacing: 4

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
                iconName: "settings"
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
                id: sftpCategory

                Layout.fillWidth: true
                title: qsTr("SFTP")
                iconName: "folder"
                actionObjectName: "settingsSftpCategory"
                selected: pane.currentCategory === "sftp"
                onActivated: pane.selectCategory("sftp")
            }

            CategoryButton {
                id: aiCategory

                Layout.fillWidth: true
                title: qsTr("AI")
                iconName: "activity"
                actionObjectName: "settingsAiCategory"
                selected: pane.currentCategory === "ai"
                onActivated: pane.selectCategory("ai")
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

            CategoryButton {
                id: aboutCategory

                Layout.fillWidth: true
                title: qsTr("About")
                iconName: "application"
                actionObjectName: "settingsAboutCategory"
                selected: pane.currentCategory === "about"
                onActivated: pane.selectCategory("about")
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
            y: pane.compactLayout ? 12 : 16
            width: Math.max(0, Math.min(1040, scrollView.availableWidth - (pane.contentInset * 2)))
            spacing: 10
            opacity: pane.contentReveal

            Text {
                text: pane.currentCategory === "application" ? qsTr("Application") : pane.currentCategory === "appearance" ? qsTr("Appearance") : pane.currentCategory === "terminal" ? qsTr("Terminal") : pane.currentCategory === "shortcuts" ? qsTr("Shortcuts") : pane.currentCategory === "sftp" ? qsTr("SFTP") : pane.currentCategory === "ai" ? qsTr("AI") : pane.currentCategory === "about" ? qsTr("About") : qsTr("Security")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                visible: true
                text: pane.currentCategory === "application" ? qsTr("Choose how the ztermy window behaves when you close it.") : pane.currentCategory === "appearance" ? qsTr("Choose the language, interface font, theme, and Windows backdrop used across ztermy.") : pane.currentCategory === "terminal" ? qsTr("Configure the global terminal font, background, cursor, selection, and paste behavior.") : pane.currentCategory === "shortcuts" ? qsTr("Search, record, unbind, and reset keyboard shortcuts for registered ztermy actions.") : pane.currentCategory === "sftp" ? qsTr("Choose the defaults applied when an SSH session opens its integrated file browser.") : pane.currentCategory === "ai" ? qsTr("Configure the model provider and the local privacy boundary used by the terminal assistant.") : pane.currentCategory === "about" ? qsTr("View ztermy identity, release information, and diagnostics.") : qsTr("Choose where SSH passwords and key passphrases are stored, unlock the portable vault, or migrate credentials safely.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
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
                visible: pane.currentCategory === "about"

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
                visible: pane.currentCategory === "about"
                compact: pane.compactLayout
                codename: "紫"
                version: Qt.application.version
                verse: "紫衣惊鸿影"
            }

            SectionCard {
                objectName: "settingsWindowBehaviorCard"
                Layout.fillWidth: true
                visible: pane.currentCategory === "application"
                heading: qsTr("Window behavior")
                compact: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    AppSwitch {
                        id: closeToTraySwitch

                        objectName: "settingsCloseToTraySwitch"
                        Layout.fillWidth: true
                        text: qsTr("Keep ztermy running in the notification area when the window is closed")
                        accessibleName: text
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The tray menu can show or hide the window and exit ztermy completely.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    AppSwitch {
                        id: performanceModeSwitch

                        objectName: "settingsPerformanceModeSwitch"
                        Layout.fillWidth: true
                        text: qsTr("Prioritize performance on software-rendered or low-power machines")
                        accessibleName: text
                        onToggled: pane.performanceModeDraft = checked
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Uses a truly opaque window, disables Windows backdrop materials, and reduces decorative motion. Your selected material is restored when this mode is turned off. Restart required.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                }
            }

            SectionCard {
                objectName: "settingsDiagnosticsCard"
                Layout.fillWidth: true
                visible: pane.currentCategory === "about"
                heading: qsTr("Diagnostics")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Export a privacy-safe environment summary for troubleshooting. Log text, crash dumps, host profiles, credentials, command history, and terminal content are never included.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 3
                        columnSpacing: Theme.spacingControl
                        rowSpacing: Theme.spacingControl

                        ActionButton {
                            objectName: "settingsExportDiagnostics"
                            Layout.fillWidth: true
                            text: qsTr("Export diagnostic report")
                            accessibleName: text
                            iconName: "save"
                            variant: "primary"
                            onClicked: diagnosticReportDialog.open()
                        }

                        ActionButton {
                            objectName: "settingsOpenLogsDirectory"
                            Layout.fillWidth: true
                            text: qsTr("Open logs folder")
                            accessibleName: text
                            iconName: "folder"
                            onClicked: {
                                const opened = pane.diagnostics.openLogsDirectory();
                                pane.presentStatus(opened ? qsTr("Logs folder opened.") : pane.diagnostics.lastError, !opened, opened);
                            }
                        }

                        ActionButton {
                            objectName: "settingsOpenCrashDirectory"
                            Layout.fillWidth: true
                            text: qsTr("Open crash reports")
                            accessibleName: text
                            iconName: "folder"
                            onClicked: {
                                const opened = pane.diagnostics.openCrashDirectory();
                                pane.presentStatus(opened ? qsTr("Crash reports folder opened.") : pane.diagnostics.lastError, !opened, opened);
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Crash dumps may contain in-memory terminal or credential data. Review them before sharing; ztermy never adds them to the exported report.")
                        color: Theme.dangerText
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "appearance"
                compact: true
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
                compact: true
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
                        model: ["acrylic", "transparent", "mica", "micaAlt", "solid"]
                        displayTextModel: [qsTr("Acrylic"), qsTr("Transparent"), "Mica", "Mica Alt", qsTr("No material (solid)")]
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
                                const alpha = pane.adjustableBackdrop ? opacitySlider.value : pane.solidBackdrop ? 1.0 : backdropBox.currentIndex === 2 ? 0.82 : 0.88;
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
                                    text: pane.adjustableBackdrop ? qsTr("%1 · %2 · %3 · %4%").arg(themeBox.effectiveDisplayText).arg(accentBox.effectiveDisplayText).arg(backdropBox.effectiveDisplayText).arg(Math.round(opacitySlider.value * 100)) : pane.solidBackdrop ? qsTr("%1 · %2 · %3 · opaque").arg(themeBox.effectiveDisplayText).arg(accentBox.effectiveDisplayText).arg(backdropBox.effectiveDisplayText) : qsTr("%1 · %2 · %3 · system controlled").arg(themeBox.effectiveDisplayText).arg(accentBox.effectiveDisplayText).arg(backdropBox.effectiveDisplayText)
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
                compact: true
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
                        implicitHeight: keepSelectionAfterCopySwitch.implicitHeight
                    }
                    AppSwitch {
                        id: keepSelectionAfterCopySwitch
                        objectName: "settingsKeepSelectionAfterCopy"
                        Layout.fillWidth: true
                        text: qsTr("Keep selection after copying")
                        accessibleName: qsTr("Keep terminal selection after copying")
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

                    Label {
                        text: qsTr("Right-click")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: rightClickBox
                        objectName: "settingsTerminalRightClick"
                        Layout.fillWidth: true
                        model: ["context-menu", "copy-paste", "paste", "select-word"]
                        displayTextModel: [qsTr("Show context menu"), qsTr("Copy selection or paste"), qsTr("Paste"), qsTr("Select word and show menu")]
                        accessibleName: qsTr("Terminal right-click behavior")
                        toolTipText: qsTr("Shift+right-click always opens the context menu.")
                    }

                    Label {
                        text: qsTr("Middle-click")
                        color: Theme.text
                    }
                    AppComboBox {
                        id: middleClickBox
                        objectName: "settingsTerminalMiddleClick"
                        Layout.fillWidth: true
                        model: ["disabled", "paste", "context-menu"]
                        displayTextModel: [qsTr("Disabled"), qsTr("Paste"), qsTr("Show context menu")]
                        accessibleName: qsTr("Terminal middle-click behavior")
                    }

                    Label {
                        text: qsTr("Word separators")
                        color: Theme.text
                    }
                    AppTextField {
                        id: wordDelimitersField
                        objectName: "settingsTerminalWordDelimiters"
                        Layout.fillWidth: true
                        maximumLength: 128
                        accessibleName: qsTr("Terminal word separator characters")
                        toolTipText: qsTr("Double-click selection stops at these characters. Paths and URLs stay intact by default.")
                    }

                    Label {
                        text: qsTr("Mouse wheel rows")
                        color: Theme.text
                    }
                    AppSpinBox {
                        id: wheelRowsBox
                        objectName: "settingsTerminalWheelRows"
                        Layout.fillWidth: true
                        from: 1
                        to: 20
                        editable: true
                        value: 3
                        accessibleName: qsTr("Rows per mouse wheel notch")
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "sftp"
                compact: true
                heading: qsTr("File browser defaults")

                GridLayout {
                    objectName: "settingsSftpGrid"
                    Layout.fillWidth: true
                    columns: pane.compactLayout ? 1 : 2
                    columnSpacing: 18
                    rowSpacing: 12

                    Label {
                        text: qsTr("Directory listing")
                        color: Theme.text
                    }
                    AppSwitch {
                        id: sftpShowHiddenSwitch

                        objectName: "settingsSftpShowHidden"
                        Layout.fillWidth: true
                        text: qsTr("Show hidden files by default")
                        accessibleName: text
                    }

                    Label {
                        text: qsTr("Destructive actions")
                        color: Theme.text
                    }
                    AppSwitch {
                        id: sftpConfirmDeleteSwitch

                        objectName: "settingsSftpConfirmDelete"
                        Layout.fillWidth: true
                        text: qsTr("Confirm before deleting remote files")
                        accessibleName: text
                    }

                    Text {
                        Layout.columnSpan: parent.columns
                        Layout.fillWidth: true
                        text: qsTr("These global defaults apply to every integrated SFTP browser. The Hidden button can still change the current session without rewriting the default.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("User skills")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Add portable AI skills as one folder per skill with a SKILL.md file. ztermy advertises only names and descriptions, then loads full instructions when you or the assistant selects a skill.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        BusyIndicator {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            visible: pane.controller.aiUserSkillsState === "loading"
                            running: visible
                        }

                        Text {
                            Layout.fillWidth: true
                            text: pane.controller.aiUserSkillsState === "loading" ? qsTr("Scanning user skills…") : pane.controller.aiUserSkillsState === "error" ? qsTr("User skills need attention") : qsTr("%1 ready · %2 warnings").arg(pane.aiUserSkillCount(true)).arg(pane.aiUserSkillCount(false))
                            color: pane.controller.aiUserSkillsState === "error" ? Theme.danger : Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                            font.weight: Font.DemiBold
                        }

                        ActionButton {
                            text: qsTr("Reload")
                            iconName: "refresh"
                            accessibleName: qsTr("Reload user skills")
                            enabled: pane.controller.aiUserSkillsState !== "loading"
                            onClicked: pane.controller.reloadAiUserSkills()
                        }

                        ActionButton {
                            text: qsTr("Open folder")
                            iconName: "folder"
                            accessibleName: qsTr("Open user skills folder")
                            onClicked: pane.controller.openAiUserSkillsDirectory()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Location: %1").arg(pane.controller.aiUserSkillsPath)
                        color: Theme.textSubtle
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiUserSkillsError.length > 0
                        text: pane.controller.aiUserSkillsError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiUserSkillsState === "ready" && pane.controller.aiUserSkills.length === 0
                        text: qsTr("No skills found yet. Open the folder to add a skill directory.")
                        color: Theme.textSubtle
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Repeater {
                        model: pane.controller.aiUserSkills

                        delegate: Rectangle {
                            id: userSkillRow

                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: userSkillContent.implicitHeight + 18
                            radius: Theme.radiusControl
                            color: Theme.controlBackground
                            border.color: modelData.ready ? Theme.border : Theme.danger

                            RowLayout {
                                id: userSkillContent

                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: "/" + userSkillRow.modelData.id
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.family: Theme.terminalFont
                                        font.pixelSize: Theme.textLabel
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: userSkillRow.modelData.description.length > 0
                                        text: userSkillRow.modelData.description
                                        color: Theme.textMuted
                                        wrapMode: Text.WordWrap
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: !userSkillRow.modelData.ready && userSkillRow.modelData.warnings.length > 0
                                        text: userSkillRow.modelData.warnings.join(" · ")
                                        color: Theme.danger
                                        wrapMode: Text.WordWrap
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }

                                Text {
                                    text: userSkillRow.modelData.ready ? qsTr("Ready") : qsTr("Warning")
                                    color: userSkillRow.modelData.ready ? Theme.success : Theme.danger
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("Model provider")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: pane.aiProviderToken() === "openai-chatgpt" ? qsTr("Use your ChatGPT subscription for ztermy's built-in terminal assistant.") : qsTr("Choose a provider, enter its API key, fetch the available models, and select one for the terminal assistant.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 2
                        columnSpacing: 18
                        rowSpacing: 10

                        Label {
                            text: qsTr("Provider")
                            color: Theme.text
                        }
                        AppComboBox {
                            id: aiProviderBox

                            objectName: "settingsAiProvider"
                            Layout.fillWidth: true
                            model: pane.aiProviderTokens
                            displayTextModel: [qsTr("OpenAI API"), qsTr("OpenAI (ChatGPT subscription)"), qsTr("Anthropic (Claude)"), qsTr("Google Gemini"), qsTr("OpenRouter"), qsTr("DeepSeek"), qsTr("Kimi"), qsTr("Alibaba Qwen"), qsTr("Z.AI (GLM)"), qsTr("Ollama"), qsTr("OpenAI-compatible")]
                            accessibleName: qsTr("AI model provider")
                            onActivated: index => pane.selectAiProvider(index)
                        }

                        Label {
                            text: qsTr("API address")
                            color: Theme.text
                            visible: pane.aiProviderToken() !== "openai-chatgpt"
                        }
                        AppTextField {
                            id: aiBaseUrlField

                            objectName: "settingsAiBaseUrl"
                            Layout.fillWidth: true
                            text: pane.aiBaseUrlDraft
                            placeholderText: qsTr("https://api.example.com")
                            accessibleName: qsTr("AI provider base URL")
                            onTextEdited: pane.aiBaseUrlDraft = text
                            visible: pane.aiProviderToken() !== "openai-chatgpt"
                        }

                        Label {
                            text: qsTr("API key")
                            color: Theme.text
                            visible: pane.aiProviderToken() !== "openai-chatgpt"
                        }
                        AppTextField {
                            id: aiApiKeyField

                            objectName: "settingsAiApiKey"
                            Layout.fillWidth: true
                            placeholderText: pane.controller.aiApiKeyConfigured ? qsTr("Saved · enter only to replace") : pane.aiProviderToken() === "ollama" ? qsTr("Not required for local Ollama") : qsTr("Enter API key")
                            passwordRevealable: true
                            accessibleName: qsTr("AI provider API key")
                            visible: pane.aiProviderToken() !== "openai-chatgpt"
                        }

                        Label {
                            text: qsTr("ChatGPT account")
                            color: Theme.text
                            visible: pane.aiProviderToken() === "openai-chatgpt"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            visible: pane.aiProviderToken() === "openai-chatgpt"

                            Text {
                                Layout.fillWidth: true
                                text: pane.controller.aiChatGptAuthState === "signed-in" ? (pane.controller.aiChatGptAccountId.length > 0 ? qsTr("Connected · %1").arg(pane.controller.aiChatGptAccountId) : qsTr("Connected")) : pane.controller.aiChatGptAuthState === "signing-in" ? qsTr("Waiting for browser authorization…") : pane.controller.aiChatGptAuthState === "device-code" ? qsTr("Waiting for device-code authorization…") : qsTr("Not connected")
                                color: pane.controller.aiChatGptAuthState === "error" ? Theme.danger : Theme.textMuted
                                elide: Text.ElideMiddle
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }

                            ActionButton {
                                text: pane.controller.aiChatGptAuthState === "signing-in" || pane.controller.aiChatGptAuthState === "device-code" ? qsTr("Cancel") : qsTr("Sign in")
                                visible: pane.controller.aiChatGptAuthState !== "signed-in"
                                onClicked: pane.controller.aiChatGptAuthState === "signing-in" || pane.controller.aiChatGptAuthState === "device-code" ? pane.controller.cancelAiChatGptSignIn() : pane.controller.beginAiChatGptSignIn()
                            }

                            ActionButton {
                                text: qsTr("Device code")
                                visible: pane.controller.aiChatGptAuthState === "signed-out" || pane.controller.aiChatGptAuthState === "error"
                                onClicked: pane.controller.beginAiChatGptDeviceSignIn()
                            }

                            ActionButton {
                                text: qsTr("Sign out")
                                visible: pane.controller.aiChatGptAuthState === "signed-in"
                                onClicked: pane.controller.signOutAiChatGpt()
                            }
                        }

                        Item {
                            visible: pane.aiProviderToken() === "openai-chatgpt" && pane.controller.aiChatGptAuthState === "device-code"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            visible: pane.aiProviderToken() === "openai-chatgpt" && pane.controller.aiChatGptAuthState === "device-code"

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Enter code %1 on the ChatGPT device page.").arg(pane.controller.aiChatGptDeviceCode.userCode || "")
                                color: Theme.text
                                wrapMode: Text.WordWrap
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textBody
                            }

                            ActionButton {
                                text: qsTr("Copy code")
                                onClicked: pane.controller.copyAiChatGptDeviceCode()
                            }

                            ActionButton {
                                text: qsTr("Open page")
                                onClicked: pane.controller.openAiChatGptDeviceVerification()
                            }
                        }

                        Label {
                            text: qsTr("Codex usage")
                            color: Theme.text
                            visible: pane.aiProviderToken() === "openai-chatgpt" && pane.controller.aiChatGptAuthState === "signed-in"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            visible: pane.aiProviderToken() === "openai-chatgpt" && pane.controller.aiChatGptAuthState === "signed-in"

                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: pane.aiSubscriptionUsageSummary()
                                color: pane.controller.aiChatGptUsage.state === "error" ? Theme.danger : Theme.textMuted
                                wrapMode: Text.WordWrap
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }

                            ActionButton {
                                text: qsTr("Refresh")
                                iconName: "refresh"
                                enabled: pane.controller.aiChatGptUsage.state !== "loading"
                                onClicked: pane.controller.refreshAiChatGptUsage()
                            }
                        }

                        Label {
                            text: qsTr("Model")
                            color: Theme.text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8

                            EditableSuggestionField {
                                id: aiModelField

                                objectName: "settingsAiModel"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                model: pane.controller.aiAvailableModels
                                text: pane.aiModelDraft
                                placeholderText: qsTr("Fetch or enter a model identifier")
                                accessibleName: qsTr("AI model identifier")
                                onTextChanged: pane.aiModelDraft = text
                            }

                            ActionButton {
                                objectName: "settingsAiFetchModels"
                                text: pane.controller.aiModelsLoading ? qsTr("Fetching…") : qsTr("Fetch models")
                                iconName: "refresh"
                                enabled: !pane.controller.aiModelsLoading && (pane.aiProviderToken() === "openai-chatgpt" ? pane.controller.aiChatGptConfigured : pane.aiBaseUrlDraft.trim().length > 0)
                                accessibleName: qsTr("Fetch models from this provider")
                                onClicked: pane.controller.refreshAiModels(pane.aiProviderToken(), pane.aiBaseUrlDraft, aiApiKeyField.text)
                            }
                        }

                        Label {
                            text: qsTr("Reasoning")
                            color: Theme.text
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            AppComboBox {
                                id: aiReasoningBox

                                objectName: "settingsAiReasoning"
                                Layout.fillWidth: true
                                model: pane.aiReasoningOptions.tokens
                                displayTextModel: pane.aiReasoningOptions.labels
                                currentIndex: pane.aiReasoningIndex(pane.aiReasoningDraft)
                                enabled: pane.aiReasoningOptions.configurable
                                accessibleName: qsTr("Model reasoning effort")
                                onActivated: index => pane.aiReasoningDraft = model[index]
                            }

                            Text {
                                Layout.fillWidth: true
                                text: pane.aiReasoningOptions.description
                                color: Theme.textMuted
                                wrapMode: Text.WordWrap
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        Label {
                            text: qsTr("Assistant permissions")
                            color: Theme.text
                        }
                        AppComboBox {
                            id: aiPermissionBox

                            objectName: "settingsAiPermission"
                            Layout.fillWidth: true
                            model: ["read-only", "ask", "auto", "yolo"]
                            displayTextModel: [qsTr("Read-only"), qsTr("Ask before changes"), qsTr("Auto except high risk"), qsTr("YOLO")]
                            toolTipModel: [qsTr("Read tools only; action and MCP tools are hidden"), qsTr("Ask in the approval card before every side effect"), qsTr("Run ordinary actions automatically; ask for high-risk commands and MCP tools"), qsTr("Run without approval prompts; explicit deny rules and safety boundaries still apply")]
                            toolTipText: toolTipModel[currentIndex] || ""
                            accessibleName: qsTr("AI terminal action permission mode")
                        }

                        Item {
                            visible: !pane.compactLayout
                            implicitHeight: aiAutomaticContextSwitch.implicitHeight
                        }
                        AppSwitch {
                            id: aiAutomaticContextSwitch

                            objectName: "settingsAiAutomaticContext"
                            Layout.fillWidth: true
                            text: qsTr("Automatically attach recent terminal context (not recommended)")
                            accessibleName: text
                        }

                        Item {
                            visible: !pane.compactLayout
                            implicitHeight: aiDebugTraceSwitch.implicitHeight
                        }
                        AppSwitch {
                            id: aiDebugTraceSwitch

                            objectName: "settingsAiDebugTrace"
                            Layout.fillWidth: true
                            text: qsTr("Record full AI request and response trace")
                            accessibleName: text
                        }

                        Item {
                            visible: !pane.compactLayout
                            implicitHeight: aiPermissionNote.implicitHeight
                        }
                        Text {
                            id: aiPermissionNote

                            Layout.fillWidth: true
                            text: qsTr("The mode supplies the default behavior. Rules created from an approval card override it for a matching action and scope.")
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Request: %1").arg(pane.controller.aiProviderEndpointPreview(pane.aiProviderToken(), pane.aiBaseUrlDraft))
                        color: Theme.textSubtle
                        elide: Text.ElideMiddle
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: aiDebugTraceSwitch.checked
                        text: pane.controller.aiDebugTracePath.length > 0 ? qsTr("Debug trace: %1").arg(pane.controller.aiDebugTracePath) : qsTr("A new JSONL trace file will be created after saving.")
                        color: Theme.warning
                        elide: Text.ElideMiddle
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiModelsError.length > 0
                        text: pane.controller.aiModelsError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.aiProviderToken() === "openai-chatgpt" && pane.controller.aiChatGptAuthError.length > 0
                        text: pane.controller.aiChatGptAuthError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: pane.aiProviderToken() === "openai-chatgpt" ? (pane.controller.aiChatGptConfigured ? qsTr("ChatGPT subscription authorization is saved in the active credential vault.") : qsTr("Sign in with a ChatGPT Plus, Pro, Business, Edu, or Enterprise account.")) : pane.controller.aiApiKeyConfigured ? qsTr("API key saved for this provider.") : qsTr("The model field remains editable when a provider does not expose a model list.")
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }

                        ActionButton {
                            objectName: "settingsAiProviderApply"
                            text: qsTr("Save")
                            accessibleName: qsTr("Save AI provider")
                            variant: "primary"
                            onClicked: {
                                const saved = pane.controller.saveAiProviderConfiguration(pane.aiProviderToken(), pane.aiBaseUrlDraft, pane.aiModelDraft, aiAutomaticContextSwitch.checked, pane.aiPermissionToken(), aiApiKeyField.text, aiDebugTraceSwitch.checked, pane.aiReasoningToken());
                                pane.presentStatus(saved ? qsTr("AI provider saved.") : qsTr("The provider settings or API key could not be saved."), !saved, saved);
                                if (saved)
                                    aiApiKeyField.text = "";
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("AI network")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Choose how model discovery, sign-in, and assistant requests reach the network. SSH, SFTP, and port forwarding are unaffected.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 2
                        columnSpacing: 18
                        rowSpacing: 10

                        Label {
                            text: qsTr("Proxy")
                            color: Theme.text
                        }
                        AppComboBox {
                            id: aiProxyBox

                            objectName: "settingsAiProxyMode"
                            Layout.fillWidth: true
                            model: ["system", "direct", "custom"]
                            displayTextModel: [qsTr("Use system proxy"), qsTr("Direct connection"), qsTr("Custom proxy")]
                            accessibleName: qsTr("AI network proxy mode")
                            onActivated: index => pane.aiProxyModeDraft = model[index]
                        }

                        Label {
                            text: qsTr("Proxy address")
                            color: Theme.text
                            visible: pane.aiProxyToken() === "custom"
                        }
                        AppTextField {
                            objectName: "settingsAiProxyUrl"
                            Layout.fillWidth: true
                            text: pane.aiProxyUrlDraft
                            visible: pane.aiProxyToken() === "custom"
                            placeholderText: qsTr("http://127.0.0.1:7890 or socks5://127.0.0.1:1080")
                            accessibleName: qsTr("AI proxy address")
                            onTextEdited: pane.aiProxyUrlDraft = text
                        }

                        Label {
                            text: qsTr("Username")
                            color: Theme.text
                            visible: pane.aiProxyToken() === "custom"
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.aiProxyUsernameDraft
                            visible: pane.aiProxyToken() === "custom"
                            placeholderText: qsTr("Optional")
                            accessibleName: qsTr("AI proxy username")
                            onTextEdited: pane.aiProxyUsernameDraft = text
                        }

                        Label {
                            text: qsTr("Password")
                            color: Theme.text
                            visible: pane.aiProxyToken() === "custom"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            visible: pane.aiProxyToken() === "custom"

                            AppTextField {
                                id: aiProxyPasswordField

                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                passwordRevealable: true
                                placeholderText: pane.controller.aiProxyPasswordConfigured ? qsTr("Saved · enter only to replace") : qsTr("Optional")
                                accessibleName: qsTr("AI proxy password")
                            }

                            ActionButton {
                                text: qsTr("Remove")
                                visible: pane.controller.aiProxyPasswordConfigured
                                onClicked: {
                                    const removed = pane.controller.removeAiProxyPassword();
                                    pane.presentStatus(removed ? qsTr("Proxy password removed.") : qsTr("The proxy password could not be removed."), !removed, removed);
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: pane.aiProxyToken() === "system" ? qsTr("Uses the Windows proxy configuration for AI traffic.") : pane.aiProxyToken() === "direct" ? qsTr("AI traffic bypasses configured proxies.") : qsTr("HTTP and SOCKS5 proxies are supported.")
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }

                        ActionButton {
                            objectName: "settingsAiProxyApply"
                            text: qsTr("Save")
                            variant: "primary"
                            enabled: pane.aiProxyToken() !== "custom" || pane.aiProxyUrlDraft.trim().length > 0
                            onClicked: {
                                const saved = pane.controller.saveAiProxySettings(pane.aiProxyToken(), pane.aiProxyUrlDraft, pane.aiProxyUsernameDraft, aiProxyPasswordField.text);
                                pane.presentStatus(saved ? qsTr("AI network settings saved.") : qsTr("The AI network settings could not be saved."), !saved, saved);
                                if (saved)
                                    aiProxyPasswordField.text = "";
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("Quick messages")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Create reusable prompts. Type / in the AI composer to search one, then edit or send the inserted text.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiQuickMessageError.length > 0
                        text: pane.controller.aiQuickMessageError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiQuickMessages.length === 0
                        text: qsTr("No quick messages yet.")
                        color: Theme.textSubtle
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Repeater {
                        model: pane.controller.aiQuickMessages

                        delegate: Rectangle {
                            id: aiQuickMessageRow

                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: aiQuickMessageRowContent.implicitHeight + 18
                            radius: Theme.radiusControl
                            color: Theme.controlBackground
                            border.color: pane.aiQuickMessageEditingId === modelData.id ? Theme.focus : Theme.border

                            RowLayout {
                                id: aiQuickMessageRowContent

                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 9

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: "/" + aiQuickMessageRow.modelData.slug + " · " + aiQuickMessageRow.modelData.name
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textLabel
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: aiQuickMessageRow.modelData.description || aiQuickMessageRow.modelData.content
                                        color: Theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }

                                ActionButton {
                                    text: qsTr("Edit")
                                    iconName: "edit"
                                    accessibleName: qsTr("Edit quick message %1").arg(aiQuickMessageRow.modelData.name)
                                    onClicked: pane.editAiQuickMessage(aiQuickMessageRow.modelData)
                                }

                                ActionButton {
                                    text: qsTr("Remove")
                                    iconName: "trash"
                                    accessibleName: qsTr("Remove quick message %1").arg(aiQuickMessageRow.modelData.name)
                                    onClicked: {
                                        const removed = pane.controller.deleteAiQuickMessage(aiQuickMessageRow.modelData.id);
                                        pane.presentStatus(removed ? qsTr("Quick message removed.") : pane.controller.aiQuickMessageError, !removed, removed);
                                        if (removed && pane.aiQuickMessageEditingId === aiQuickMessageRow.modelData.id)
                                            pane.resetAiQuickMessageDraft();
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: aiQuickMessageEditor.implicitHeight + 22
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.border

                        ColumnLayout {
                            id: aiQuickMessageEditor

                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: pane.aiQuickMessageEditingId.length > 0 ? qsTr("Edit quick message") : qsTr("New quick message")
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textBody
                                font.weight: Font.DemiBold
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: pane.compactLayout ? 1 : 2
                                columnSpacing: 8
                                rowSpacing: 8

                                AppTextField {
                                    Layout.fillWidth: true
                                    text: pane.aiQuickMessageNameDraft
                                    placeholderText: qsTr("Name")
                                    accessibleName: qsTr("Quick message name")
                                    onTextEdited: {
                                        pane.aiQuickMessageNameDraft = text;
                                        if (!pane.aiQuickMessageSlugManual)
                                            pane.aiQuickMessageSlugDraft = pane.normalizeAiQuickMessageSlug(text);
                                    }
                                }

                                AppTextField {
                                    Layout.fillWidth: true
                                    text: pane.aiQuickMessageSlugDraft
                                    placeholderText: qsTr("Slash command, for example service-status")
                                    accessibleName: qsTr("Quick message slash command")
                                    onTextEdited: {
                                        pane.aiQuickMessageSlugManual = true;
                                        pane.aiQuickMessageSlugDraft = pane.normalizeAiQuickMessageSlug(text.replace(/^\/+/, ""));
                                    }
                                }
                            }

                            AppTextField {
                                Layout.fillWidth: true
                                text: pane.aiQuickMessageDescriptionDraft
                                placeholderText: qsTr("Short description (optional)")
                                accessibleName: qsTr("Quick message description")
                                onTextEdited: pane.aiQuickMessageDescriptionDraft = text
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 132
                                clip: true

                                TextArea {
                                    text: pane.aiQuickMessageContentDraft
                                    placeholderText: qsTr("Prompt text inserted into the AI composer")
                                    color: Theme.text
                                    placeholderTextColor: Theme.textMuted
                                    selectionColor: Theme.accent
                                    selectedTextColor: Theme.accentText
                                    wrapMode: TextEdit.Wrap
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                    Accessible.name: qsTr("Quick message prompt")
                                    onTextChanged: pane.aiQuickMessageContentDraft = text
                                    background: Rectangle {
                                        color: Theme.controlBackground
                                        radius: Theme.radiusControl
                                        border.color: parent.activeFocus ? Theme.focus : Theme.border
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: pane.aiQuickMessageSlugDraft.length > 0 ? "/" + pane.aiQuickMessageSlugDraft : qsTr("Use lowercase letters, numbers, and hyphens.")
                                    color: Theme.textSubtle
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                }

                                ActionButton {
                                    text: qsTr("New")
                                    enabled: pane.aiQuickMessageEditingId.length > 0 || pane.aiQuickMessageNameDraft.length > 0 || pane.aiQuickMessageContentDraft.length > 0
                                    onClicked: pane.resetAiQuickMessageDraft()
                                }

                                ActionButton {
                                    text: pane.aiQuickMessageEditingId.length > 0 ? qsTr("Save changes") : qsTr("Add quick message")
                                    variant: "primary"
                                    enabled: pane.aiQuickMessageNameDraft.trim().length > 0 && pane.aiQuickMessageSlugDraft.length > 0 && pane.aiQuickMessageContentDraft.trim().length > 0
                                    onClicked: pane.saveAiQuickMessageDraft()
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("Assistant permission rules")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Ask mode can remember an allow or deny choice for this session, one saved Profile, or all Profiles. Exact, prefix, wildcard, and regular-expression matching are supported.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiPermissionRuleError.length > 0
                        text: pane.controller.aiPermissionRuleError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiPermissionRules.length === 0
                        text: qsTr("No remembered assistant rules yet.")
                        color: Theme.textSubtle
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Repeater {
                        model: pane.controller.aiPermissionRules

                        delegate: Rectangle {
                            id: aiRuleRow

                            required property var modelData
                            property string patternDraft: modelData.pattern
                            property bool enabledDraft: modelData.enabled
                            Layout.fillWidth: true
                            implicitHeight: aiRuleContent.implicitHeight + 18
                            radius: Theme.radiusControl
                            color: Theme.controlBackground
                            border.color: Theme.border

                            ColumnLayout {
                                id: aiRuleContent

                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 7

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 7

                                    Rectangle {
                                        Layout.preferredWidth: 8
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: aiRuleRow.modelData.decision === "allow" ? Theme.success : aiRuleRow.modelData.decision === "deny" ? Theme.danger : Theme.warning
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "%1 · %2 · %3".arg(aiRuleRow.modelData.decision === "allow" ? qsTr("Allow") : aiRuleRow.modelData.decision === "deny" ? qsTr("Deny") : qsTr("Ask")).arg(pane.aiRuleCapabilityLabel(aiRuleRow.modelData.capability)).arg(pane.aiRuleScopeLabel(aiRuleRow.modelData))
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textLabel
                                        font.weight: Font.DemiBold
                                    }

                                    AppSwitch {
                                        id: aiRuleEnabledSwitch

                                        checked: aiRuleRow.enabledDraft
                                        accessibleName: qsTr("Enable this assistant rule")
                                        onToggled: aiRuleRow.enabledDraft = checked
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: pane.compactLayout ? 1 : 2
                                    columnSpacing: 8
                                    rowSpacing: 7

                                    AppComboBox {
                                        id: aiRuleMatcherBox

                                        Layout.fillWidth: true
                                        model: ["exact", "prefix", "glob", "regex", "all"]
                                        displayTextModel: [qsTr("Exact action"), qsTr("Starts with"), qsTr("Wildcard"), qsTr("Regular expression"), qsTr("Any action of this type")]
                                        currentIndex: pane.aiRuleMatcherIndex(aiRuleRow.modelData.matcher)
                                        accessibleName: qsTr("Rule matcher")
                                    }

                                    AppTextField {
                                        Layout.fillWidth: true
                                        visible: aiRuleMatcherBox.currentValue !== "all"
                                        compact: true
                                        text: aiRuleRow.patternDraft
                                        placeholderText: qsTr("Command or action pattern")
                                        accessibleName: placeholderText
                                        onTextEdited: aiRuleRow.patternDraft = text
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    ActionButton {
                                        text: qsTr("Remove")
                                        iconName: "trash"
                                        accessibleName: qsTr("Remove this assistant permission rule")
                                        onClicked: pane.controller.deleteAiPermissionRule(aiRuleRow.modelData.id)
                                    }

                                    ActionButton {
                                        text: qsTr("Save")
                                        iconName: "save"
                                        variant: "primary"
                                        accessibleName: qsTr("Save this assistant permission rule")
                                        onClicked: {
                                            const saved = pane.controller.updateAiPermissionRule(aiRuleRow.modelData.id, aiRuleMatcherBox.currentValue, aiRuleRow.patternDraft, aiRuleRow.enabledDraft);
                                            pane.presentStatus(saved ? qsTr("Assistant rule saved.") : qsTr("The assistant rule could not be saved."), !saved, saved);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("MCP extensions")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("MCP servers run as local stdio child processes. Trust and review a server once; calls then follow the current assistant mode and reusable rules.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.mcpOperationError.length > 0
                        text: pane.controller.mcpOperationError
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    Repeater {
                        model: pane.controller.mcpServers

                        delegate: Rectangle {
                            id: mcpServerRow

                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: mcpServerContent.implicitHeight + 18
                            radius: Theme.radiusControl
                            color: Theme.controlBackground
                            border.color: Theme.border

                            RowLayout {
                                id: mcpServerContent
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: mcpServerRow.modelData.state === "ready" ? Theme.success : mcpServerRow.modelData.state === "error" ? Theme.danger : Theme.textMuted
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: mcpServerRow.modelData.namespace + "  ·  " + mcpServerRow.modelData.id
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textLabel
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: mcpServerRow.modelData.state + (mcpServerRow.modelData.error.length > 0 ? " · " + mcpServerRow.modelData.error : "")
                                        color: mcpServerRow.modelData.state === "error" ? Theme.danger : Theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }

                                ActionButton {
                                    text: qsTr("Edit")
                                    accessibleName: qsTr("Edit MCP server %1").arg(mcpServerRow.modelData.id)
                                    onClicked: pane.editMcpServer(mcpServerRow.modelData)
                                }

                                ActionButton {
                                    text: qsTr("Restart")
                                    accessibleName: qsTr("Restart MCP server %1").arg(mcpServerRow.modelData.id)
                                    onClicked: {
                                        const restarted = pane.controller.restartMcpServer(mcpServerRow.modelData.id);
                                        pane.presentStatus(restarted ? qsTr("MCP server restarted.") : qsTr("The MCP server could not be restarted."), !restarted, restarted);
                                    }
                                }

                                ActionButton {
                                    text: qsTr("Remove")
                                    accessibleName: qsTr("Remove MCP server %1").arg(mcpServerRow.modelData.id)
                                    variant: "danger"
                                    onClicked: {
                                        const removed = pane.controller.removeMcpServer(mcpServerRow.modelData.id);
                                        pane.presentStatus(removed ? qsTr("MCP server removed.") : qsTr("The MCP server could not be removed."), !removed, removed);
                                    }
                                }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 2
                        columnSpacing: 18
                        rowSpacing: 10

                        Label {
                            text: qsTr("Server id")
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.mcpEditingId
                            enabled: pane.mcpOriginalId.length === 0
                            placeholderText: qsTr("local-files")
                            accessibleName: qsTr("MCP server id")
                            onTextEdited: pane.mcpEditingId = text
                        }

                        Label {
                            text: qsTr("Tool namespace")
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.mcpNamespaceDraft
                            placeholderText: qsTr("files")
                            accessibleName: qsTr("MCP tool namespace")
                            onTextEdited: pane.mcpNamespaceDraft = text
                        }

                        Label {
                            text: qsTr("Executable")
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.mcpProgramDraft
                            placeholderText: qsTr("Absolute path to node.exe or server.exe")
                            accessibleName: qsTr("MCP executable path")
                            onTextEdited: pane.mcpProgramDraft = text
                        }

                        Label {
                            text: qsTr("Arguments")
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.mcpArgumentsDraft
                            placeholderText: qsTr("[\"server.js\", \"--stdio\"]")
                            accessibleName: qsTr("MCP arguments as a JSON string array")
                            onTextEdited: pane.mcpArgumentsDraft = text
                        }

                        Label {
                            text: qsTr("Working directory")
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: pane.mcpWorkingDirectoryDraft
                            placeholderText: qsTr("Optional absolute directory")
                            accessibleName: qsTr("MCP working directory")
                            onTextEdited: pane.mcpWorkingDirectoryDraft = text
                        }

                        Label {
                            text: qsTr("Server trust")
                            color: Theme.text
                        }
                        AppComboBox {
                            id: mcpTrustBox
                            Layout.fillWidth: true
                            currentIndex: 1
                            model: ["disabled", "observe", "execute"]
                            displayTextModel: [qsTr("Disabled"), qsTr("Discover only"), qsTr("Allow reviewed tools")]
                            accessibleName: qsTr("MCP server trust")
                        }

                        Item {
                            visible: !pane.compactLayout
                            implicitHeight: mcpEnabledSwitch.implicitHeight
                        }
                        AppSwitch {
                            id: mcpEnabledSwitch
                            Layout.fillWidth: true
                            checked: true
                            text: qsTr("Start this stdio server with ztermy")
                            accessibleName: text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ActionButton {
                            text: qsTr("New server")
                            accessibleName: text
                            onClicked: pane.resetMcpDraft()
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ActionButton {
                            text: pane.mcpOriginalId.length > 0 ? qsTr("Save server") : qsTr("Add server")
                            accessibleName: text
                            variant: "primary"
                            enabled: pane.mcpEditingId.length > 0 && pane.mcpNamespaceDraft.length > 0 && pane.mcpProgramDraft.length > 0
                            onClicked: pane.saveMcpDraft()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: pane.mcpReviewTool !== null
                        implicitHeight: mcpReviewColumn.implicitHeight + 22
                        radius: Theme.radiusControl
                        color: Theme.raisedBackground
                        border.color: Theme.focus

                        ColumnLayout {
                            id: mcpReviewColumn
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: pane.mcpReviewTool ? pane.mcpReviewTool.exposedName : ""
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textBody
                                font.weight: Font.DemiBold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: pane.mcpReviewTool ? qsTr("Untrusted server description: %1").arg(pane.mcpReviewTool.description) : ""
                                color: Theme.textMuted
                                wrapMode: Text.WordWrap
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }

                            Text {
                                Layout.fillWidth: true
                                text: pane.mcpReviewTool ? qsTr("Schema digest: %1").arg(pane.mcpReviewTool.schemaDigest) : ""
                                color: Theme.textSoft
                                wrapMode: Text.WrapAnywhere
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 180
                                clip: true

                                TextArea {
                                    readOnly: true
                                    selectByMouse: true
                                    text: pane.mcpReviewTool ? pane.mcpReviewTool.inputSchema : ""
                                    color: Theme.text
                                    selectionColor: Theme.accent
                                    selectedTextColor: Theme.accentText
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                    wrapMode: TextEdit.NoWrap
                                    background: Rectangle {
                                        color: Theme.controlBackground
                                        radius: Theme.radiusControl
                                        border.color: Theme.border
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Approval is invalidated automatically if the description or schema changes.")
                                    color: Theme.textMuted
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                                ActionButton {
                                    text: qsTr("Close")
                                    onClicked: pane.mcpReviewTool = null
                                }
                                ActionButton {
                                    text: pane.mcpReviewTool && pane.mcpReviewTool.approved ? qsTr("Revoke") : qsTr("Approve exact schema")
                                    variant: pane.mcpReviewTool && pane.mcpReviewTool.approved ? "danger" : "primary"
                                    onClicked: {
                                        const tool = pane.mcpReviewTool;
                                        const changed = pane.controller.setMcpToolApproved(tool.serverId, tool.exposedName, tool.schemaDigest, !tool.approved);
                                        pane.presentStatus(changed ? (tool.approved ? qsTr("MCP tool approval revoked.") : qsTr("MCP tool schema approved.")) : qsTr("The MCP tool approval could not be changed."), !changed, changed);
                                        pane.mcpReviewTool = null;
                                    }
                                }
                            }
                        }
                    }

                    Repeater {
                        model: pane.controller.mcpTools

                        delegate: Rectangle {
                            id: mcpToolRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: mcpToolContent.implicitHeight + 18
                            radius: Theme.radiusControl
                            color: "transparent"
                            border.color: Theme.border

                            RowLayout {
                                id: mcpToolContent
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: mcpToolRow.modelData.exposedName
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.family: Theme.terminalFont
                                        font.pixelSize: Theme.textLabel
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: mcpToolRow.modelData.approved ? qsTr("Approved exact schema") : qsTr("Review required")
                                        color: mcpToolRow.modelData.approved ? Theme.success : Theme.warning
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }

                                ActionButton {
                                    text: qsTr("Review")
                                    accessibleName: qsTr("Review schema for %1").arg(mcpToolRow.modelData.exposedName)
                                    onClicked: pane.mcpReviewTool = mcpToolRow.modelData
                                }
                            }
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "ai"
                compact: true
                heading: qsTr("Encrypted conversation history")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    AppSwitch {
                        id: aiConversationHistorySwitch

                        objectName: "settingsAiConversationHistory"
                        Layout.fillWidth: true
                        text: qsTr("Keep bounded AI conversations after restart")
                        accessibleName: text
                        onToggled: {
                            if (pane.loadingDraft) {
                                return;
                            }
                            const saved = pane.controller.setAiConversationHistoryEnabled(checked);
                            pane.presentStatus(saved ? (checked ? qsTr("Encrypted conversation history enabled.") : qsTr("Conversation history retention disabled.")) : qsTr("Choose Windows Credential Manager or an unlocked portable vault before enabling history."), !saved, saved);
                            if (!saved) {
                                checked = pane.controller.aiConversationHistoryEnabled;
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("History is off by default. Transcript bodies use authenticated encryption; only the small data key is stored in the active credential vault. Disabling retention does not delete existing encrypted history.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.aiConversationHistory.errorCode.length > 0
                        text: qsTr("History unavailable: %1").arg(pane.controller.aiConversationHistory.errorCode)
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%n saved conversation(s)", "", pane.controller.aiConversationHistory.count)
                            color: Theme.textSoft
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }

                        ActionButton {
                            text: qsTr("Export decrypted JSON")
                            accessibleName: qsTr("Export decrypted AI conversation history")
                            enabled: !pane.controller.aiConversationHistory.busy && pane.controller.aiConversationHistory.count > 0
                            onClicked: aiHistoryExportDialog.open()
                        }

                        ActionButton {
                            text: qsTr("Delete history")
                            accessibleName: qsTr("Delete encrypted AI conversation history and its key")
                            variant: "destructive"
                            enabled: !pane.controller.aiConversationHistory.busy && pane.controller.aiConversationHistory.count > 0
                            onClicked: aiHistoryDeleteDialog.open()
                        }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                visible: pane.currentCategory === "security"
                compact: true
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
                compact: true
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
                compact: true
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
                objectName: "settingsStatusMessage"
                Layout.fillWidth: true
                visible: pane.statusMessage.length > 0
                opacity: pane.statusVisible ? 1.0 : 0.0
                text: pane.statusMessage
                kind: pane.statusIsError ? "error" : "success"

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                        easing.type: Easing.InOutCubic
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                visible: pane.currentCategory === "application" || pane.currentCategory === "appearance" || pane.currentCategory === "terminal" || pane.currentCategory === "sftp"
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
                        pane.presentStatus(reset ? qsTr("Default settings restored.") : qsTr("Default settings could not be restored."), !reset, reset);
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
                        pane.presentStatus(qsTr("Unsaved changes discarded."), false, true);
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

    FileDialog {
        id: diagnosticReportDialog

        title: qsTr("Export diagnostic report")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Diagnostic reports (*.json)"), qsTr("All files (*)")]
        defaultSuffix: "json"
        onAccepted: {
            const exported = pane.diagnostics.exportReport(selectedFile);
            pane.presentStatus(exported ? qsTr("Diagnostic report exported.") : pane.diagnostics.lastError, !exported, exported);
        }
    }

    FileDialog {
        id: aiHistoryExportDialog

        title: qsTr("Export decrypted AI conversation history")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        defaultSuffix: "json"
        onAccepted: pane.controller.aiConversationHistory.exportDecrypted(selectedFile.toString())
    }

    ConfirmationDialog {
        id: aiHistoryDeleteDialog

        heading: qsTr("Delete all AI conversation history?")
        description: qsTr("This removes the encrypted history, its recovery copy, and the data-encryption key. This action cannot be undone.")
        acceptText: qsTr("Delete history")
        destructive: true
        onAccepted: pane.controller.aiConversationHistory.clear()
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
