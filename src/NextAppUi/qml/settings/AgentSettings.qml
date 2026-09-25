import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import NextAppUi
import Nextapp.Models 1.0

ScrollView {
    id: root
    anchors.fill: parent
    Settings { id: settings }
    property string mcpCredential: NaCore.mcpCredential()

    function load() {
        enabled.checked = settings.value("ai/mcp/enabled", false)
        listeningAddress.text = settings.value("ai/mcp/listenAddress", "127.0.0.1")
        port.value = settings.value("ai/mcp/port", 3120)
        createGate.currentIndex = createGate.indexOfValue(settings.value("ai/mcp/gate/createAction", "Disabled"))
        updateGate.currentIndex = updateGate.indexOfValue(settings.value("ai/mcp/gate/updateAction", "Disabled"))
        completeGate.currentIndex = completeGate.indexOfValue(settings.value("ai/mcp/gate/completeAction", "Disabled"))
        createNodeGate.currentIndex = createNodeGate.indexOfValue(settings.value("ai/mcp/gate/createNode", "Disabled"))
        updateNodeGate.currentIndex = updateNodeGate.indexOfValue(settings.value("ai/mcp/gate/updateNode", "Disabled"))
    }
    function commit() {
        settings.setValue("ai/mcp/enabled", enabled.checked)
        settings.setValue("ai/mcp/listenAddress", listeningAddress.text.trim())
        settings.setValue("ai/mcp/port", port.value)
        settings.setValue("ai/mcp/gate/createAction", createGate.currentValue)
        settings.setValue("ai/mcp/gate/updateAction", updateGate.currentValue)
        settings.setValue("ai/mcp/gate/completeAction", completeGate.currentValue)
        settings.setValue("ai/mcp/gate/createNode", createNodeGate.currentValue)
        settings.setValue("ai/mcp/gate/updateNode", updateNodeGate.currentValue)
        settings.sync()
    }
    Component.onCompleted: load()
    onVisibleChanged: if (visible) load()

    GridLayout {
        width: root.width - 15
        columns: width >= 420 ? 2 : 1
        rowSpacing: 8
        Label { text: qsTr("Local MCP interface") }
        CheckBox { id: enabled; text: qsTr("Enable local MCP interface") }
        Label { text: qsTr("Listening IP") }
        TextField {
            id: listeningAddress
            Layout.fillWidth: true
            placeholderText: "127.0.0.1"
            inputMethodHints: Qt.ImhNoPredictiveText
        }
        Label { text: qsTr("Listening port") }
        SpinBox {
            id: port
            from: 1024
            to: 65535
            editable: true
            Layout.fillWidth: true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("The default port is stored so the local agent can use the same URL after a restart")
        }
        Label { text: qsTr("MCP endpoint") }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: NaCore.mcpEndpoint
            }
            ToolButton {
                icon.name: "edit-copy"
                display: AbstractButton.IconOnly
                enabled: NaCore.mcpEndpoint.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy MCP URL")
                onClicked: NaCore.copyToClipboard(NaCore.mcpEndpoint)
            }
        }
        Label { text: qsTr("MCP credential") }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ToolButton {
                icon.name: "edit-copy"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy MCP credential")
                onClicked: NaCore.copyToClipboard(root.mcpCredential)
            }
            ToolButton {
                icon.name: "view-refresh"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Rotate MCP credential")
                onClicked: root.mcpCredential = NaCore.rotateMcpCredential()
            }
        }
        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: NaCore.mcpEndpoint.length > 0
                  ? qsTr("The URL is stable across restarts while this port remains available.")
                  : qsTr("Not listening. Enable AI and this interface, then connect NextApp to the server.")
        }
        Label { text: qsTr("Create actions") }
        ComboBox { id: createGate; textRole: "text"; valueRole: "value"; model: [{text: qsTr("Disabled"), value: "Disabled"}, {text: qsTr("Ask every time"), value: "Ask"}, {text: qsTr("Always allow"), value: "Always allow"}] }
        Label { text: qsTr("Update actions") }
        ComboBox { id: updateGate; textRole: "text"; valueRole: "value"; model: [{text: qsTr("Disabled"), value: "Disabled"}, {text: qsTr("Ask every time"), value: "Ask"}, {text: qsTr("Always allow"), value: "Always allow"}] }
        Label { text: qsTr("Complete actions") }
        ComboBox { id: completeGate; textRole: "text"; valueRole: "value"; model: [{text: qsTr("Disabled"), value: "Disabled"}, {text: qsTr("Ask every time"), value: "Ask"}, {text: qsTr("Always allow"), value: "Always allow"}] }
        Label { text: qsTr("Create lists/nodes") }
        ComboBox { id: createNodeGate; textRole: "text"; valueRole: "value"; model: [{text: qsTr("Disabled"), value: "Disabled"}, {text: qsTr("Ask every time"), value: "Ask"}, {text: qsTr("Always allow"), value: "Always allow"}] }
        Label { text: qsTr("Update lists/nodes") }
        ComboBox { id: updateNodeGate; textRole: "text"; valueRole: "value"; model: [{text: qsTr("Disabled"), value: "Disabled"}, {text: qsTr("Ask every time"), value: "Ask"}, {text: qsTr("Always allow"), value: "Always allow"}] }
        Label { text: qsTr("Recent agent activity") }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, contentHeight)
            clip: true
            model: NaCore.mcpActivityHistory
            delegate: Label {
                required property var modelData
                width: ListView.view.width
                elide: Text.ElideRight
                text: "%1: %2 — %3".arg(modelData.operation || "MCP").arg(modelData.state || "").arg(modelData.requestId || "")
            }
        }
    }
}
