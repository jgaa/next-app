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
        if (!NaCore.isMobile) {
            settings.setValue("client/maxInstances", instances.value)
        }
        settings.sync()
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2

        Label {
            visible: !NaCore.isMobile
            text: qsTr("Instances")
        }

        RowLayout {
            visible: !NaCore.isMobile

            Slider {
                id: instances
                Layout.fillWidth: true
                visible: !NaCore.isMobile
                from: 1
                to: 10
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: parseInt(settings.value("client/maxInstances"))
            }

            Text {
                property string singleton: qsTr("Singleton")
                Layout.preferredWidth: textMetrics.boundingRect(text).width + 10 // Add some padding
                text: instances.value == 1 ? singleton : instances.value.toFixed(0)
            }
        }

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
            text: qsTr("Reconnect")
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
            text: qsTr("Do a full re-synch when\nconnecting to the server")
            checked: settings.value("sync/resync") === "true"
        }

        Label {
            text: qsTr("Resend")
        }

        CheckBox {
            id: resend
            text: qsTr("Re-send pending requests")
            checked: settings.value("server/resend_requests") === "true"
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
