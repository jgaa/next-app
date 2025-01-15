import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

pragma ComponentBehavior: Bound

Item {
    anchors.fill: parent

    Settings {
        id: settings
    }

    function commit() {
        settings.setValue("logging/path", logPath.text)
        settings.setValue("logging/level", uiStyle.currentIndex.toString())
        settings.setValue("logging/prune", prune.checked ? "true" : "false")
        settings.setValue("sync/resync", resync.checked ? "true" : "false")
        settings.setValue("server/reconnect_level", reconnectLevel.currentIndex.toString())
        settings.setValue("server/resend_requests", resend.checked ? "true" : "false")
        settings.sync()
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Logfile")}
        RowLayout {
            DlgInputField {
                Layout.fillWidth: true
                id: logPath
                text: settings.value("logging/path")
            }

            // Does not work in Ubuntu 23.10. The system goes into a loop of opening the file
            // RoundButton {
            //     id: viewLogBtn
            //     text: qsTr("View")
            //     onClicked: {
            //         NaCore.openFile(logPath.text)
            //     }
            // }
        }

        Label { text: qsTr("Log Level")}
        ComboBox {
            id: uiStyle
            currentIndex: parseInt(settings.value("logging/level"))
            Layout.fillWidth: true
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")]
        }

        Item {}

        CheckBox {
            id: prune
            text: qsTr("Prune log when starting")
            checked: settings.value("logging/prune") == "true"
        }

        Label {
            text: qsTr("Reconnect to the server")
            // Line wrap
            wrapMode: Text.WordWrap
            }
        ComboBox {
            id: reconnectLevel
            currentIndex: parseInt(settings.value("server/reconnect_level"))
            Layout.fillWidth: true
            model: [
                qsTr("When Online"),
                qsTr("When on Site"),
                qsTr("When Local network comes up"),
                qsTr("Never")]
        }

        Label {
            text: qsTr("Clear cache")
            color: "red"
        }

        CheckBox {
            id: resync
            text: qsTr("Do a full re-synch when connecting to the server")
            checked: settings.value("sync/resync") === "true"
        }

        Label {
            text: qsTr("Resend")
            color: "red"
        }

        CheckBox {
            id: resend
            text: qsTr("Re-send unconfirmed requests to the server.")
            checked: settings.value("server/resend_requests") === "true"
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
