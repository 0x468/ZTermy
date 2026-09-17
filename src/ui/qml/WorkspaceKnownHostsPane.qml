pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: root

    objectName: "workspaceKnownHostsPane"

    required property var controller
    signal createHostRequested(string host, int port)
    readonly property var store: controller.knownHosts
    property var pendingRemoval: ({})
    property bool automaticImportCompleted: false
    property bool compactRows: false
    property int sortMode: 0
    property string filterText: ""
    readonly property var filteredEntries: {
        const query = filterText.trim().toLocaleLowerCase();
        let result = query.length === 0 ? store.entries.slice(0) : store.entries.filter(entry => {
            return (entry.endpoint || "").toLocaleLowerCase().includes(query) || (entry.algorithm || "").toLocaleLowerCase().includes(query) || (entry.fingerprint || "").toLocaleLowerCase().includes(query);
        });
        result.sort((left, right) => {
            if (sortMode === 1)
                return String(right.endpoint).localeCompare(String(left.endpoint));
            if (sortMode === 2) {
                const algorithm = String(left.algorithm).localeCompare(String(right.algorithm));
                return algorithm !== 0 ? algorithm : String(left.endpoint).localeCompare(String(right.endpoint));
            }
            return String(left.endpoint).localeCompare(String(right.endpoint));
        });
        return result;
    }

    color: Theme.contentBackground

    Timer {
        interval: 100
        repeat: true
        running: root.visible && !root.automaticImportCompleted
        onTriggered: {
            if (!root.store.busy) {
                root.automaticImportCompleted = true;
                root.store.importDefaultOpenSsh(true);
                stop();
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                RowLayout {
                    spacing: 8

                    Text {
                        text: qsTr("Known hosts")
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }

                    Rectangle {
                        implicitWidth: countLabel.implicitWidth + 14
                        implicitHeight: 24
                        radius: 12
                        color: Theme.selectedBackground

                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: qsTr("%1 keys").arg(root.store.count)
                            color: Theme.accent
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Review the SSH host keys trusted by ztermy, or import keys already saved by OpenSSH on this computer.")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                    wrapMode: Text.Wrap
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: 8

                ActionButton {
                    objectName: "knownHostsImportCurrent"
                    text: qsTr("Import this computer")
                    iconName: "download"
                    enabled: !root.store.busy
                    onClicked: root.store.importDefaultOpenSsh()
                }

                ActionButton {
                    text: qsTr("Import file")
                    iconName: "folder"
                    enabled: !root.store.busy
                    onClicked: importDialog.open()
                }

                ActionButton {
                    text: qsTr("Refresh")
                    iconName: "refresh"
                    enabled: !root.store.busy
                    onClicked: root.store.refresh()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            AppTextField {
                id: searchField
                Layout.fillWidth: true
                accessibleName: qsTr("Search known hosts")
                placeholderText: qsTr("Search host, algorithm, or fingerprint")
                text: root.filterText
                onTextEdited: root.filterText = text
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: 8

                AppComboBox {
                    width: 150
                    accessibleName: qsTr("Sort known hosts")
                    model: ["az", "za", "algorithm"]
                    displayTextModel: [qsTr("Host A-Z"), qsTr("Host Z-A"), qsTr("Algorithm")]
                    currentIndex: root.sortMode
                    onActivated: index => root.sortMode = index
                }

                ActionButton {
                    text: root.compactRows ? qsTr("Details") : qsTr("Compact")
                    iconName: root.compactRows ? "tree" : "list"
                    onClicked: root.compactRows = !root.compactRows
                }

                ActionButton {
                    visible: root.store.count > 0
                    text: qsTr("Clear all")
                    iconName: "trash"
                    variant: "destructive"
                    enabled: !root.store.busy
                    onClicked: clearDialog.openFrom(this)
                }
            }
        }

        StatusMessage {
            Layout.fillWidth: true
            kind: "error"
            text: root.store.operationError
        }

        StatusMessage {
            Layout.fillWidth: true
            kind: "success"
            visible: Object.keys(root.store.lastImportSummary).length > 0 && root.store.operationError.length === 0
            text: visible ? qsTr("Imported %1 keys; %2 duplicates, %3 conflicts, and %4 unsupported entries were left unchanged.").arg(root.store.lastImportSummary.added || 0).arg(root.store.lastImportSummary.duplicates || 0).arg(root.store.lastImportSummary.conflicts || 0).arg(root.store.lastImportSummary.unsupported || 0) : ""
        }

        StatePanel {
            Layout.fillWidth: true
            visible: root.store.busy && root.store.count === 0
            kind: "loading"
            centered: true
            heading: qsTr("Loading known hosts")
            description: qsTr("Reading trusted SSH host keys in the background.")
        }

        StatePanel {
            Layout.fillWidth: true
            visible: !root.store.busy && root.store.count === 0 && root.store.operationError.length === 0
            centered: true
            heading: qsTr("No trusted hosts yet")
            description: root.height < 420 ? "" : qsTr("Host keys accepted while connecting appear here. You can also import your current OpenSSH known_hosts file.")
        }

        StatePanel {
            Layout.fillWidth: true
            visible: !root.store.busy && root.store.count > 0 && root.filteredEntries.length === 0
            centered: true
            heading: qsTr("No matching known hosts")
            description: qsTr("Try a different host name, algorithm, or fingerprint.")
        }

        ListView {
            id: hostList

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.filteredEntries.length > 0
            clip: true
            model: root.filteredEntries
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: hostCard

                required property var modelData
                width: ListView.view.width
                height: cardContent.implicitHeight + 24
                radius: Theme.radiusControl
                color: cardHover.hovered ? Theme.controlHover : Theme.raisedBackground
                border.color: Theme.border

                HoverHandler {
                    id: cardHover
                }

                ColumnLayout {
                    id: cardContent

                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                            radius: Theme.radiusControl
                            color: Theme.selectedBackground

                            AppIcon {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                name: "hosts"
                                color: Theme.accent
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: hostCard.modelData.endpoint
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    text: hostCard.modelData.algorithm
                                    color: Theme.accent
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: hostCard.modelData.fingerprint
                                color: Theme.textMuted
                                elide: Text.ElideMiddle
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textLabel
                            }

                            Text {
                                Layout.fillWidth: true
                                text: hostCard.modelData.publicKey
                                visible: !root.compactRows
                                color: Theme.textSubtle
                                elide: Text.ElideMiddle
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        spacing: 8

                        ActionButton {
                            text: qsTr("Create host")
                            iconName: "plus"
                            enabled: !root.store.busy
                            onClicked: root.createHostRequested(hostCard.modelData.host, hostCard.modelData.port)
                        }

                        ActionButton {
                            text: qsTr("Fingerprint")
                            iconName: "copy"
                            enabled: !root.store.busy
                            onClicked: root.store.copyText(hostCard.modelData.fingerprint)
                        }

                        ActionButton {
                            text: qsTr("Public key")
                            iconName: "copy"
                            enabled: !root.store.busy
                            onClicked: root.store.copyText(hostCard.modelData.publicKey)
                        }

                        ActionButton {
                            text: qsTr("Remove")
                            iconName: "trash"
                            variant: "destructive"
                            enabled: !root.store.busy
                            onClicked: {
                                root.pendingRemoval = hostCard.modelData;
                                removeDialog.openFrom(this);
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import OpenSSH known_hosts")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OpenSSH known hosts (known_hosts*)"), qsTr("All files (*)")]
        onAccepted: root.store.importOpenSshFile(selectedFile.toString())
    }

    ConfirmationDialog {
        id: removeDialog
        heading: qsTr("Remove trusted host key?")
        description: root.pendingRemoval.endpoint ? qsTr("The next connection to %1 will ask you to verify this host key again.").arg(root.pendingRemoval.endpoint) : ""
        acceptText: qsTr("Remove")
        destructive: true
        onAccepted: {
            root.store.removeEntry(root.pendingRemoval.host, root.pendingRemoval.port, root.pendingRemoval.algorithmToken);
            root.pendingRemoval = ({});
        }
        onRejected: root.pendingRemoval = ({})
    }

    ConfirmationDialog {
        id: clearDialog
        heading: qsTr("Clear all trusted host keys?")
        description: qsTr("Every SSH host will require host-key verification the next time it connects.")
        acceptText: qsTr("Clear all")
        destructive: true
        onAccepted: root.store.clearAll()
    }
}
