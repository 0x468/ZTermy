pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    required property var controller
    property var activeTab: null
    signal browseHostsRequested
    signal terminalRequested

    readonly property var connectedSshTabs: controller.terminalTabs.filter(tab => tab.kind === "ssh" && tab.running)
    readonly property bool remoteAvailable: activeTab !== null && activeTab.kind === "ssh" && activeTab.running

    function ensureRemoteBrowser() {
        if (!visible || !remoteAvailable)
            return;
        controller.ensureSftpBrowser();
    }

    color: Theme.workspaceBackground
    onVisibleChanged: Qt.callLater(ensureRemoteBrowser)
    onActiveTabChanged: Qt.callLater(ensureRemoteBrowser)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("SFTP files")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 20
                font.weight: Font.Bold
            }

            Item {
                Layout.fillWidth: true
            }

            AppComboBox {
                Layout.preferredWidth: 280
                visible: pane.connectedSshTabs.length > 0
                model: pane.connectedSshTabs.map(tab => tab.id)
                displayTextModel: pane.connectedSshTabs.map(tab => tab.title + " · " + tab.identity)
                accessibleName: qsTr("SFTP terminal session")
                currentIndex: Math.max(0, model.indexOf(pane.controller.activeTerminalTabId))
                onActivated: index => {
                    pane.controller.activateTerminalTab(model[index]);
                    Qt.callLater(pane.ensureRemoteBrowser);
                }
            }

            ActionButton {
                text: qsTr("Browse hosts")
                iconName: "hosts"
                onClicked: pane.browseHostsRequested()
            }
        }

        AppSplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            leadingPane: localPane
            trailingPane: remotePane
            onRatioEdited: value => ratio = value

            Rectangle {
                id: localPane
                SplitView.minimumWidth: 280
                color: Theme.raisedBackground
                border.color: Theme.border
                radius: Theme.radiusPanel

                LocalFilesPane {
                    anchors.fill: parent
                    controller: pane.controller
                    onInsertRequested: path => {
                        if (pane.controller.insertLocalFilePath(path))
                            pane.terminalRequested();
                    }
                }
            }

            Rectangle {
                id: remotePane
                SplitView.minimumWidth: 280
                SplitView.fillWidth: true
                color: Theme.raisedBackground
                border.color: Theme.border
                radius: Theme.radiusPanel

                SftpBrowser {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: pane.remoteAvailable
                    controller: pane.controller
                }

                StatePanel {
                    anchors.centerIn: parent
                    width: Math.min(420, parent.width - 40)
                    visible: !pane.remoteAvailable
                    kind: "empty"
                    centered: true
                    heading: qsTr("Select a connected SSH terminal")
                    description: qsTr("The remote file browser uses the SSH session selected above. Connect a host first when no session is available.")
                }
            }
        }
    }
}
