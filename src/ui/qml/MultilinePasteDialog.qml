pragma ComponentBehavior: Bound

import QtQuick

ConfirmationDialog {
    id: control
    required property var controller
    property string ownerWindowId: "main"
    property var pendingViewport: null
    property int lineCount: 0
    property int promptChoice: 0

    function openFor(viewport, count) {
        pendingViewport = viewport;
        lineCount = count;
        promptChoice = 0;
        Qt.callLater(() => {
            if (pendingViewport)
                openFrom(pendingViewport);
        });
    }
    function cancelPending() {
        const viewport = pendingViewport;
        pendingViewport = null;
        lineCount = 0;
        if (viewport)
            viewport.resolveMultilinePaste(false);
    }

    heading: qsTranslate("Main", "Paste multiple lines?")
    description: qsTranslate("Main", "The clipboard contains %n line(s). Pasting may execute commands immediately in the active terminal.", "", lineCount)
    acceptText: qsTranslate("Main", "Paste")
    acceptObjectName: "multilinePasteAccept"
    rejectObjectName: "multilinePasteReject"
    extraContent: Component {
        AppComboBox {
            objectName: "multilinePasteScope"
            model: [qsTr("Ask every time"), qsTr("Do not ask again during this run"), qsTr("Never ask again")]
            accessibleName: qsTr("Multi-line paste confirmation")
            currentIndex: control.promptChoice
            onActivated: index => control.promptChoice = index
        }
    }
    onAccepted: {
        const viewport = pendingViewport;
        pendingViewport = null;
        lineCount = 0;
        if (viewport && viewport.multilinePastePending) {
            // Consume the accepted bytes before a policy notification disables
            // the viewport's guard and clears its pending clipboard content.
            viewport.resolveMultilinePaste(true);
            controller.applyMultilinePastePromptChoice(["once", "run", "never"][promptChoice], ownerWindowId);
        }
    }
    onRejected: cancelPending()
    onClosed: cancelPending()
    Connections {
        target: control.pendingViewport
        function onMultilinePastePendingChanged() {
            if (control.pendingViewport && !control.pendingViewport.multilinePastePending && control.visible)
                control.close();
        }
    }
}
