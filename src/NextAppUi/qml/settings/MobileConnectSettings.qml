import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore

ScrollView {
    id: root
    anchors.fill: parent

    Settings {
        id: settings
        category: "mobileConnect"
    }

    function load() {
        enableServer.checked = settings.value("enabled", false)
        listeningAddress.text = settings.value("listenAddress", "0.0.0.0")
        port.value = settings.value("port", 45437)
    }

    function commit() {
        settings.setValue("enabled", enableServer.checked)
        settings.setValue("listenAddress", listeningAddress.text.trim())
        settings.setValue("port", port.value)
        settings.sync()
    }

    GridLayout {
        width: root.width - 15
        columns: width >= 440 ? 2 : 1
        rowSpacing: 8

        Label { text: qsTr("Mobile Connect server") }
        CheckBox { id: enableServer; text: qsTr("Enable server") }

        Label { text: qsTr("Listen address") }
        TextField {
            id: listeningAddress
            Layout.fillWidth: true
            placeholderText: "0.0.0.0"
            inputMethodHints: Qt.ImhNoPredictiveText
        }

        Label { text: qsTr("Listening port") }
        SpinBox {
            id: port
            from: 1024
            to: 65535
            editable: true
            Layout.fillWidth: true
        }

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Accept idea transfers from the SmTool mobile app over your local network. Each transfer requires you to compare and approve its eight digit code. The server starts when NextApp launches only if enabled.")
        }

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: NaCore.mobileConnectRunning ? "green" : "red"
            text: NaCore.mobileConnectRunning
                ? qsTr("Listening on %1:%2").arg(NaCore.mobileConnectListenAddress).arg(NaCore.mobileConnectPort)
                : enableServer.checked ? qsTr("The server is enabled but is not listening.") : qsTr("The server is disabled.")
        }
    }

    Component.onCompleted: load()
}
