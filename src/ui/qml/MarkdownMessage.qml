import QtQuick

TextEdit {
    id: markdown

    property string source: ""

    text: source
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: true
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.uiFont
    font.pixelSize: Theme.textBody
    renderType: Text.QtRendering
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Rendered Markdown message")
    onLinkActivated: link => Qt.openUrlExternally(link)

    HoverHandler {
        cursorShape: markdown.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
    }
}
