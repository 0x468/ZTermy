pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: markdown

    property string source: ""
    property color color: Theme.text
    property font font: Qt.font({
        family: Theme.uiFont,
        pixelSize: Theme.textBody
    })
    property int textFormat: TextEdit.MarkdownText
    property string renderedSource: ""

    readonly property var blocks: splitBlocks(renderedSource, textFormat)

    signal copyRequested(string text)

    implicitHeight: contentColumn.implicitHeight
    clip: true
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Rendered Markdown message")

    Component.onCompleted: renderedSource = source

    onSourceChanged: {
        if (!renderTimer.running)
            renderTimer.start();
    }

    Timer {
        id: renderTimer

        interval: 24
        repeat: false
        onTriggered: markdown.renderedSource = markdown.source
    }

    function tableCells(line) {
        let value = line.trim();
        if (value.startsWith("|"))
            value = value.slice(1);
        if (value.endsWith("|"))
            value = value.slice(0, -1);
        return value.split("|").map(cell => cell.trim());
    }

    function isTableSeparator(line) {
        const cells = tableCells(line);
        return cells.length > 0 && cells.every(cell => /^:?-{3,}:?$/.test(cell));
    }

    function tableWidths(rows) {
        let columns = 0;
        for (const row of rows)
            columns = Math.max(columns, row.length);
        let widths = [];
        for (let column = 0; column < columns; ++column) {
            let longest = 0;
            for (const row of rows)
                longest = Math.max(longest, column < row.length ? row[column].length : 0);
            widths.push(Math.max(88, Math.min(260, longest * markdown.font.pixelSize * 0.62 + 24)));
        }
        return widths;
    }

    function splitBlocks(value, format) {
        if (format !== TextEdit.MarkdownText)
            return [
                {
                    kind: "text",
                    content: value
                }
            ];

        const lines = value.replace(/\r\n/g, "\n").split("\n");
        let result = [];
        let prose = [];
        const flushProse = () => {
            if (prose.length > 0) {
                result.push({
                    kind: "markdown",
                    content: prose.join("\n")
                });
                prose = [];
            }
        };

        for (let index = 0; index < lines.length; ) {
            // Fenced blocks with 3+ backticks (or tildes) and an optional
            // language tag; the closing fence must match the opener's length
            // but we stay lenient for unclosed fences at the end of input.
            const fence = lines[index].match(/^\s*(```+|~~~+)\s*([^\s`]*)\s*$/);
            if (fence) {
                flushProse();
                const fenceLength = fence[1].length;
                const language = fence[2] || "";
                const closing = new RegExp("^\\s*" + (fence[1][0] === "`" ? "`" : "~") + "{" + fenceLength + "}\\s*$");
                let body = [];
                ++index;
                while (index < lines.length && !closing.test(lines[index])) {
                    body.push(lines[index]);
                    ++index;
                }
                if (index < lines.length)
                    ++index;
                result.push({
                    kind: "code",
                    language: language,
                    content: body.join("\n")
                });
                continue;
            }

            if (index + 1 < lines.length && lines[index].includes("|") && isTableSeparator(lines[index + 1])) {
                flushProse();
                let rows = [tableCells(lines[index])];
                index += 2;
                while (index < lines.length && lines[index].includes("|") && lines[index].trim().length > 0) {
                    rows.push(tableCells(lines[index]));
                    ++index;
                }
                result.push({
                    kind: "table",
                    rows: rows,
                    widths: tableWidths(rows)
                });
                continue;
            }

            prose.push(lines[index]);
            ++index;
        }
        flushProse();
        return result;
    }

    ColumnLayout {
        id: contentColumn

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        Repeater {
            model: markdown.blocks

            delegate: Loader {
                id: blockLoader

                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                sourceComponent: modelData.kind === "code" ? codeBlock : modelData.kind === "table" ? tableBlock : proseBlock
                onLoaded: item.blockData = blockLoader.modelData
            }
        }
    }

    Component {
        id: proseBlock

        TextEdit {
            property var blockData

            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: blockData.content
            textFormat: blockData.kind === "markdown" ? TextEdit.MarkdownText : TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.WrapAnywhere
            color: markdown.color
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font: markdown.font
            renderType: Text.QtRendering
            onLinkActivated: link => Qt.openUrlExternally(link)

            HoverHandler {
                cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }
    }

    Component {
        id: codeBlock

        Rectangle {
            id: codeCard

            property var blockData
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: codeColumn.implicitHeight + 2
            radius: Theme.radiusControl
            color: Theme.controlBackground
            border.color: Theme.border
            clip: true

            ColumnLayout {
                id: codeColumn

                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 5
                    Layout.bottomMargin: 3
                    spacing: 6

                    Rectangle {
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: languagePill.implicitWidth + 14
                        radius: 4
                        color: Theme.controlPressed

                        Text {
                            id: languagePill

                            anchors.centerIn: parent
                            text: codeCard.blockData.language.length > 0 ? codeCard.blockData.language.toUpperCase() : qsTr("CODE")
                            color: Theme.textSoft
                            font.family: Theme.terminalFont
                            font.pixelSize: Theme.textCompact
                            font.weight: Font.DemiBold
                        }
                    }

                    Button {
                        id: copyCodeButton

                        property bool copied: false
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 26
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: copied ? qsTr("Code copied") : qsTr("Copy code")
                        onClicked: {
                            markdown.copyRequested(codeCard.blockData.content);
                            copied = true;
                            copiedTimer.restart();
                        }

                        contentItem: AppIcon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            name: copyCodeButton.copied ? "check" : "copy"
                            color: copyCodeButton.copied ? Theme.successText : Theme.textMuted
                        }

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: copyCodeButton.down ? Theme.controlPressed : copyCodeButton.hovered ? Theme.controlHover : "transparent"
                            border.color: copyCodeButton.activeFocus ? Theme.focus : "transparent"
                            border.width: copyCodeButton.activeFocus ? 2 : 1
                        }

                        AppToolTip {
                            text: copyCodeButton.copied ? qsTr("Copied") : qsTr("Copy code")
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        Timer {
                            id: copiedTimer

                            interval: 1600
                            onTriggered: copyCodeButton.copied = false
                        }
                    }
                }

                Flickable {
                    id: codeViewport

                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: codeText.implicitHeight + (horizontalBar.visible ? horizontalBar.height + 3 : 0) + 14
                    clip: true
                    contentWidth: Math.max(width, codeText.implicitWidth + 20)
                    contentHeight: height
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    TextEdit {
                        id: codeText

                        x: 10
                        y: 7
                        width: Math.max(codeViewport.width - 20, implicitWidth)
                        text: codeCard.blockData.content
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        persistentSelection: true
                        wrapMode: TextEdit.NoWrap
                        color: markdown.color
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        font.family: Theme.terminalFont
                        font.pixelSize: markdown.font.pixelSize
                        renderType: Text.QtRendering
                    }

                    ScrollBar.horizontal: ScrollBar {
                        id: horizontalBar
                        policy: codeViewport.contentWidth > codeViewport.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }
                }
            }
        }
    }

    Component {
        id: tableBlock

        Rectangle {
            id: tableCard

            property var blockData
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: tableRows.implicitHeight + (tableBar.visible ? tableBar.height + 3 : 0)
            radius: Theme.radiusControl
            color: Theme.controlBackground
            border.color: Theme.border
            clip: true

            Flickable {
                id: tableViewport

                anchors.fill: parent
                clip: true
                contentWidth: Math.max(width, tableRows.implicitWidth)
                contentHeight: tableRows.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Column {
                    id: tableRows

                    Repeater {
                        model: tableCard.blockData.rows

                        delegate: Row {
                            id: tableRow

                            required property var modelData
                            required property int index

                            Repeater {
                                model: tableCard.blockData.widths.length

                                delegate: Rectangle {
                                    id: tableCellFrame

                                    required property int index
                                    width: tableCard.blockData.widths[tableCellFrame.index]
                                    height: tableCell.implicitHeight + 14
                                    color: tableRow.index === 0 ? Theme.raisedBackground : "transparent"
                                    border.color: Theme.border

                                    TextEdit {
                                        id: tableCell

                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        text: tableCellFrame.index < tableRow.modelData.length ? tableRow.modelData[tableCellFrame.index] : ""
                                        textFormat: TextEdit.MarkdownText
                                        readOnly: true
                                        selectByMouse: true
                                        persistentSelection: true
                                        wrapMode: TextEdit.Wrap
                                        color: markdown.color
                                        selectionColor: Theme.accent
                                        selectedTextColor: Theme.accentText
                                        font.family: markdown.font.family
                                        font.pixelSize: markdown.font.pixelSize
                                        font.weight: tableRow.index === 0 ? Font.DemiBold : Font.Normal
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollBar.horizontal: ScrollBar {
                    id: tableBar
                    policy: tableViewport.contentWidth > tableViewport.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
            }
        }
    }
}
