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
        listeningAddress.text = settings.value("ai/mcp/listen_address", "127.0.0.1")
        port.value = settings.value("ai/mcp/port", 58421)
        createGate.currentIndex = createGate.indexOfValue(settings.value("ai/mcp/gate/create_action", "Disabled"))
        updateGate.currentIndex = updateGate.indexOfValue(settings.value("ai/mcp/gate/update_action", "Disabled"))
        completeGate.currentIndex = completeGate.indexOfValue(settings.value("ai/mcp/gate/complete_action", "Disabled"))
    }
    function commit() {
        settings.setValue("ai/mcp/enabled", enabled.checked)
        settings.setValue("ai/mcp/listen_address", listeningAddress.text.trim())
        settings.setValue("ai/mcp/port", port.value)
        settings.setValue("ai/mcp/gate/create_action", createGate.currentValue)
        settings.setValue("ai/mcp/gate/update_action", updateGate.currentValue)
        settings.setValue("ai/mcp/gate/complete_action", completeGate.currentValue)
        settings.sync()
    }
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
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: root.mcpCredential
            }
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
    }
}
