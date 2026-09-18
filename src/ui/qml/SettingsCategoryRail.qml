pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings navigation (UI V2 chapter 5): categories grouped by intent with a
// search field that jumps to a category and highlights the matching row.
Rectangle {
    id: rail

    property string currentCategory: "application"
    property bool compact: false
    readonly property bool searching: searchField.text.trim().length > 0
    readonly property var categoryTitles: ({
            "application": qsTr("Application"),
            "appearance": qsTr("Appearance"),
            "terminal": qsTr("Terminal"),
            "shortcuts": qsTr("Shortcuts"),
            "sftp": qsTr("SFTP"),
            "security": qsTr("Security"),
            "ai": qsTr("AI"),
            "about": qsTr("About")
        })
    // Search index: [row key, category, title, extra keywords]. An empty key
    // only opens the category.
    readonly property var index: [["language", "appearance", qsTr("Display language"), "locale english chinese"], ["uiFontFamily", "appearance", qsTr("Interface font"), "ui typeface"], ["theme", "appearance", qsTr("Theme"), "dark light system mode"], ["accent", "appearance", qsTr("Accent color"), "highlight windows custom"], ["effectsTier", "appearance", qsTr("Visual effects"), "animation motion shadow material reduced"], ["backdrop", "appearance", qsTr("Windows backdrop"), "acrylic mica transparent solid"], ["backdropOpacity", "appearance", qsTr("Window background opacity"), "transparency"], ["terminalTheme", "appearance", qsTr("Terminal theme"), "palette color scheme ansi"], ["terminalFontFamily", "terminal", qsTr("Font family"), "monospace typeface"], ["terminalFontSize", "terminal", qsTr("Font size"), "zoom"], ["terminalLigatures", "terminal", qsTr("Ligatures"), "font"], ["terminalBackgroundOpacity", "terminal", qsTr("Terminal background opacity"), "transparency"], ["localShell", "terminal", qsTr("Local shell"), "powershell cmd wsl"], ["cursor", "terminal", qsTr("Cursor"), "block bar underline blink"], ["copyOnSelect", "terminal", qsTr("Copy on select"), "clipboard selection"], ["confirmMultilinePaste", "terminal", qsTr("Confirm multi-line paste"), "clipboard safety"], ["terminalRightClick", "terminal", qsTr("Right-click"), "mouse context menu paste"], ["terminalMiddleClick", "terminal", qsTr("Middle-click"), "mouse paste"], ["terminalWordDelimiters", "terminal", qsTr("Word separators"), "double click selection"], ["terminalScrollRows", "terminal", qsTr("Mouse wheel rows"), "scroll speed"], ["", "terminal", qsTr("Selection action popup"), "copy ai search highlight"], ["", "application", qsTr("Window behavior"), "tray close single instance tab double click"], ["", "shortcuts", qsTr("Keyboard shortcuts"), "keys bindings"], ["sftpShowHiddenFiles", "sftp", qsTr("Show hidden files"), "dotfiles browser"], ["sftpConfirmDelete", "sftp", qsTr("Confirm before deleting"), "remote files"], ["", "ai", qsTr("Model provider"), "openai anthropic gemini ollama api key"], ["", "ai", qsTr("AI network"), "proxy"], ["", "ai", qsTr("MCP extensions"), "tools servers"], ["", "security", qsTr("Credential storage"), "password vault keychain portable"], ["", "about", qsTr("Diagnostics"), "logs crash report version"]]
    readonly property var matches: searching ? searchMatches(searchField.text) : []

    signal categoryActivated(string category)
    signal rowRequested(string category, string key)

    component RailHeading: Text {
        property bool first: false

        Layout.leftMargin: 4
        Layout.topMargin: first ? 0 : 10
        Layout.bottomMargin: 2
        visible: !rail.searching
        color: Theme.textSubtle
        font.family: Theme.uiFont
        font.pixelSize: 10
        font.letterSpacing: 1.2
        font.weight: Font.DemiBold
    }

    function searchMatches(text) {
        const needle = text.trim().toLocaleLowerCase();
        const hits = [];
        for (const entry of index) {
            const haystack = (entry[2] + " " + entry[3] + " " + categoryTitle(entry[1])).toLocaleLowerCase();
            if (haystack.indexOf(needle) >= 0) {
                hits.push(entry);
            }
            if (hits.length >= 8) {
                break;
            }
        }
        return hits;
    }

    function categoryTitle(category) {
        return categoryTitles[category] || "";
    }

    function focusCategory(category) {
        const buttons = [applicationCategory, appearanceCategory, terminalCategory, shortcutsCategory, sftpCategory, securityCategory, aiCategory, aboutCategory];
        for (const button of buttons) {
            if (button.category === category) {
                button.focusAction();
                return;
            }
        }
    }

    function focusSearch() {
        searchField.forceActiveFocus();
    }

    function activateMatch(entry) {
        rowRequested(entry[1], entry[0]);
        searchField.text = "";
    }

    objectName: "settingsCategoryRail"
    width: compact ? 140 : 208
    color: Theme.panelBackground

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: rail.compact ? 8 : 10
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: column

            width: flick.width
            height: Math.max(implicitHeight, flick.height)
            spacing: 4

            AppTextField {
                id: searchField

                objectName: "settingsSearch"
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                compact: true
                placeholderText: qsTr("Search settings")
                accessibleName: qsTr("Search settings")
                Keys.onReturnPressed: {
                    if (rail.matches.length > 0) {
                        rail.activateMatch(rail.matches[0]);
                    }
                }
                Keys.onEscapePressed: text = ""
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: rail.searching
                spacing: 2

                Text {
                    Layout.leftMargin: 4
                    visible: rail.matches.length === 0
                    text: qsTr("No matching settings")
                    color: Theme.textSubtle
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }

                Repeater {
                    model: rail.matches

                    delegate: Rectangle {
                        id: match

                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: 40
                        radius: Theme.radiusControl
                        color: matchAction.hovered || matchAction.visualFocus ? Theme.controlHover : "transparent"
                        border.color: matchAction.visualFocus ? Theme.focus : "transparent"
                        border.width: 1

                        Behavior on color {
                            MotionColor {}
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Text {
                                width: parent.width
                                text: match.modelData[2]
                                color: Theme.text
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }

                            Text {
                                width: parent.width
                                text: rail.categoryTitle(match.modelData[1])
                                color: Theme.textSubtle
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        KeyboardAction {
                            id: matchAction

                            anchors.fill: parent
                            accessibleName: qsTr("Open %1").arg(match.modelData[2])
                            onActivated: rail.activateMatch(match.modelData)
                        }
                    }
                }
            }

            RailHeading {
                text: qsTr("GENERAL")
                first: true
            }

            SettingsCategoryButton {
                id: applicationCategory

                Layout.fillWidth: true
                category: "application"
                title: rail.categoryTitle("application")
                iconName: "settings"
                actionObjectName: "settingsApplicationCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "application"
                onActivated: rail.categoryActivated("application")
            }

            SettingsCategoryButton {
                id: appearanceCategory

                Layout.fillWidth: true
                category: "appearance"
                title: rail.categoryTitle("appearance")
                iconName: "appearance"
                actionObjectName: "settingsAppearanceCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "appearance"
                onActivated: rail.categoryActivated("appearance")
            }

            SettingsCategoryButton {
                id: terminalCategory

                Layout.fillWidth: true
                category: "terminal"
                title: rail.categoryTitle("terminal")
                iconName: "terminal"
                actionObjectName: "settingsTerminalCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "terminal"
                onActivated: rail.categoryActivated("terminal")
            }

            SettingsCategoryButton {
                id: shortcutsCategory

                Layout.fillWidth: true
                category: "shortcuts"
                title: rail.categoryTitle("shortcuts")
                iconName: "shortcuts"
                actionObjectName: "settingsShortcutsCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "shortcuts"
                onActivated: rail.categoryActivated("shortcuts")
            }

            RailHeading {
                text: qsTr("CONNECTIONS")
                first: false
            }

            SettingsCategoryButton {
                id: sftpCategory

                Layout.fillWidth: true
                category: "sftp"
                title: rail.categoryTitle("sftp")
                iconName: "folder"
                actionObjectName: "settingsSftpCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "sftp"
                onActivated: rail.categoryActivated("sftp")
            }

            SettingsCategoryButton {
                id: securityCategory

                Layout.fillWidth: true
                category: "security"
                title: rail.categoryTitle("security")
                iconName: "security"
                actionObjectName: "settingsSecurityCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "security"
                onActivated: rail.categoryActivated("security")
            }

            RailHeading {
                text: qsTr("ASSISTANT")
                first: false
            }

            SettingsCategoryButton {
                id: aiCategory

                Layout.fillWidth: true
                category: "ai"
                title: rail.categoryTitle("ai")
                iconName: "activity"
                actionObjectName: "settingsAiCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "ai"
                onActivated: rail.categoryActivated("ai")
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 4
                visible: !rail.searching
                implicitHeight: 1
                color: Theme.border
            }

            SettingsCategoryButton {
                id: aboutCategory

                Layout.fillWidth: true
                category: "about"
                title: rail.categoryTitle("about")
                iconName: "application"
                actionObjectName: "settingsAboutCategory"
                visible: !rail.searching
                selected: rail.currentCategory === "about"
                onActivated: rail.categoryActivated("about")
            }

            Item {
                Layout.fillHeight: true
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                visible: !rail.compact
                text: qsTr("Stored locally")
                color: Theme.textSubtle
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }
        }
    }
}
