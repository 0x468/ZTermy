import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: header

    required property var activity
    required property bool hasDetails
    required property bool expanded
    required property string stateLabel

    objectName: "aiToolActivityToggle"
    Layout.fillWidth: true
    Layout.preferredHeight: 34
    clip: true
    hoverEnabled: header.hasDetails
    focusPolicy: header.hasDetails ? Qt.StrongFocus : Qt.NoFocus
    enabled: header.hasDetails
    Accessible.name: header.hasDetails ? (header.expanded ? qsTranslate("AiAssistantPane", "Collapse tool details") : qsTranslate("AiAssistantPane", "Expand tool details")) + " · " + header.activity.name : header.activity.name
    contentItem: RowLayout {
        spacing: 7

        BusyIndicator {
            id: toolBusy

            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            running: header.activity.state === "queued" || header.activity.state === "running" || header.activity.state === "executing" || header.activity.state === "awaiting_approval"
            visible: running
        }

        AppIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            visible: !toolBusy.visible
            name: header.activity.state === "succeeded" ? "check" : header.activity.state === "cancelled" || header.activity.state === "failed" ? "close" : header.activity.sideEffecting ? "terminal" : "search"
            color: header.activity.state === "succeeded" ? Theme.successText : header.activity.state === "failed" ? Theme.dangerText : Theme.textMuted
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: header.activity.name
                color: Theme.text
                elide: Text.ElideRight
                font.family: Theme.terminalFont
                font.pixelSize: Theme.textCompact
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                visible: header.activity.summary.length > 0
                objectName: "aiToolSummary"
                // A summary is a single preview row, not the raw
                // multiline command. Full text stays in details.
                text: header.activity.summary.replace(/\s+/g, " ").trim()
                textFormat: Text.PlainText
                maximumLineCount: 1
                color: Theme.textMuted
                elide: Text.ElideRight
                font.family: Theme.terminalFont
                font.pixelSize: Theme.textCompact
            }
        }

        Text {
            text: header.stateLabel
            color: header.activity.state === "succeeded" ? Theme.successText : header.activity.state === "failed" ? Theme.dangerText : header.activity.state === "cancelled" ? Theme.warning : Theme.textMuted
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: Theme.textCompact
            font.weight: Font.Medium
        }

        AppIcon {
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            visible: header.hasDetails
            name: header.expanded ? "chevron-down" : "chevron-right"
            color: Theme.textMuted
        }
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: "transparent"
        border.color: header.visualFocus ? Theme.focus : "transparent"
        border.width: header.visualFocus ? 2 : 0
    }
}
