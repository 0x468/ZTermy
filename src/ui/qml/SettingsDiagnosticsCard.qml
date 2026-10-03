import QtQuick
import QtQuick.Layouts

SectionCard {
    id: card
    required property var diagnostics
    required property bool compactLayout
    signal exportRequested
    signal statusRequested(string text, bool failed, bool success)
    objectName: "settingsDiagnosticsCard"
    Layout.fillWidth: true
    heading: qsTranslate("SettingsPane", "Diagnostics")

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingControl

        Text {
            Layout.fillWidth: true
            text: qsTranslate("SettingsPane", "Export a privacy-safe environment summary for troubleshooting. Log text, crash dumps, host profiles, credentials, command history, and terminal content are never included.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }

        GridLayout {
            Layout.fillWidth: true
            columns: card.compactLayout ? 1 : 3
            columnSpacing: Theme.spacingControl
            rowSpacing: Theme.spacingControl

            ActionButton {
                objectName: "settingsExportDiagnostics"
                Layout.fillWidth: true
                text: qsTranslate("SettingsPane", "Export diagnostic report")
                accessibleName: text
                iconName: "save"
                variant: "primary"
                onClicked: card.exportRequested()
            }

            ActionButton {
                objectName: "settingsOpenLogsDirectory"
                Layout.fillWidth: true
                text: qsTranslate("SettingsPane", "Open logs folder")
                accessibleName: text
                iconName: "folder"
                onClicked: {
                    const opened = card.diagnostics.openLogsDirectory();
                    card.statusRequested(opened ? qsTranslate("SettingsPane", "Logs folder opened.") : card.diagnostics.lastError, !opened, opened);
                }
            }

            ActionButton {
                objectName: "settingsOpenCrashDirectory"
                Layout.fillWidth: true
                text: qsTranslate("SettingsPane", "Open crash reports")
                accessibleName: text
                iconName: "folder"
                onClicked: {
                    const opened = card.diagnostics.openCrashDirectory();
                    card.statusRequested(opened ? qsTranslate("SettingsPane", "Crash reports folder opened.") : card.diagnostics.lastError, !opened, opened);
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTranslate("SettingsPane", "Crash dumps may contain in-memory terminal or credential data. Review them before sharing; ztermy never adds them to the exported report.")
            color: Theme.dangerText
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }
    }
}
