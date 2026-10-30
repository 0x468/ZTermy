pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: page
    required property bool compactLayout
    required property var diagnostics
    signal exportRequested
    signal statusRequested(string text, bool failed, bool success)
    spacing: 10

    SectionCard {
        Layout.fillWidth: true

        RowLayout {
            objectName: "settingsApplicationBrandLockup"
            Layout.fillWidth: true
            Layout.minimumHeight: page.compactLayout ? 116 : 188
            spacing: page.compactLayout ? 16 : 36
            Accessible.name: qsTranslate("SettingsPane", "ztermy SSH Terminal")
            Accessible.role: Accessible.Graphic

            Image {
                Layout.preferredWidth: page.compactLayout ? 92 : 168
                Layout.preferredHeight: page.compactLayout ? 92 : 168
                Layout.alignment: Qt.AlignVCenter
                source: "image://ztermy-brand/app-icon/" + (Theme.dark ? "000001" : "000002") + "/" + (page.compactLayout ? "compact" : "regular")
                sourceSize.width: page.compactLayout ? 184 : 336
                sourceSize.height: page.compactLayout ? 184 : 336
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                cache: false
                smooth: true
                mipmap: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: page.compactLayout ? 3 : 8

                Row {
                    spacing: 0

                    Text {
                        text: qsTranslate("SettingsPane", "Z")
                        color: "#2AA8FF"
                        font.family: Theme.uiFont
                        font.pixelSize: page.compactLayout ? 38 : 72
                        font.weight: Font.Bold
                    }

                    Text {
                        text: qsTranslate("SettingsPane", "termy")
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: page.compactLayout ? 38 : 72
                        font.weight: Font.Bold
                    }
                }

                Text {
                    text: qsTranslate("SettingsPane", "SSH TERMINAL")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: page.compactLayout ? 11 : 18
                    font.letterSpacing: page.compactLayout ? 3 : 6
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "settingsRuntimeIdentity"
                    Layout.fillWidth: true
                    text: ApplicationInfo.platformDescription + " · " + ApplicationInfo.buildCommit
                    color: Theme.textSubtle
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: page.compactLayout ? Theme.textCompact : Theme.textLabel

                    TapHandler {
                        objectName: "settingsBuildCommitCopy"
                        onDoubleTapped: ApplicationInfo.copyBuildCommit()
                    }
                }
            }
        }
    }

    ReleaseIdentityCard {
        objectName: "settingsReleaseIdentityCard"
        Layout.fillWidth: true
        compact: page.compactLayout
        codename: "汐"
        version: Qt.application.version
        verse: "半轮月，一道水"
    }

    SettingsDiagnosticsCard {
        Layout.fillWidth: true
        diagnostics: page.diagnostics
        compactLayout: page.compactLayout
        onExportRequested: page.exportRequested()
        onStatusRequested: (text, failed, success) => page.statusRequested(text, failed, success)
    }
}
