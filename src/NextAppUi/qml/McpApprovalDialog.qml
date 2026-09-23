import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models 1.0

Dialog {
    id: root
    parent: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    title: qsTr("Approve agent operation")
    width: Math.min(parent.width - 32, 560)
    x: (parent.width - width) / 2
    y: Math.max(16, (parent.height - height) / 3)

    property var operation: ({})

    function refresh() {
        operation = NaCore.mcpPendingApproval
        if (operation.requestId) open()
        else close()
    }

    Connections {
        target: NaCore
        function onMcpPendingApprovalChanged() { root.refresh() }
    }
    Component.onCompleted: refresh()

    contentItem: ColumnLayout {
        spacing: 10
        Label { text: qsTr("Agent: %1").arg(root.operation.agent || qsTr("Local agent")); font.bold: true }
        Label { text: qsTr("Operation: %1").arg(root.operation.operation || "") }
        Label { text: qsTr("Target: %1").arg(root.operation.target || "") ; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: qsTr("Proposed changes"); font.bold: true }
        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, contentHeight + topPadding + bottomPadding)
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: JSON.stringify(root.operation.changes || {}, null, 2)
        }
        Label { text: qsTr("Agent explanation (untrusted text)"); font.bold: true; visible: (root.operation.reason || "").length > 0 }
        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(120, contentHeight + topPadding + bottomPadding)
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: root.operation.reason || ""
            visible: text.length > 0
        }
        CheckBox {
            id: alwaysAllow
            text: qsTr("Always allow %1 for this local agent").arg(root.operation.operation || "operation")
        }
    }
    footer: DialogButtonBox {
        Button {
            text: qsTr("Reject")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: NaCore.resolveMcpApproval(root.operation.requestId, false, false)
        }
        Button {
            text: qsTr("Approve")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            highlighted: true
            onClicked: NaCore.resolveMcpApproval(root.operation.requestId, true, alwaysAllow.checked)
        }
    }
}
