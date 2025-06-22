import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb
import "../common.js" as Common

pragma ComponentBehavior: Bound

ScrollView {
    id: root
    anchors.fill: parent

    Settings {
        id: settings
    }

    function commit() {
        settings.setValue("logging/path", logPath.text)
        settings.setValue("logging/level", logLevelFile.currentIndex.toString())
        settings.setValue("logging/applevel", logLevelApp.currentIndex.toString())
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
        width: root.width - 15
        rowSpacing: 4
        columns: width >= 350 ? 2 : 1

        Label {
            text: qsTr("Log Level\n(Application)")
            visible: logLevelApp.visible
        }
        ComboBox {
            id: logLevelApp
            visible: Qt.platform.os === "linux"
                     || Qt.platform.os === "android"

            currentIndex: parseInt(settings.value("logging/applevel"))
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

        Label { text: qsTr("Log Level\n(File)")}
        ComboBox {
            id: logLevelFile
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

        Label { text: qsTr("Logfile")}
        RowLayout {
            DlgInputField {
                Layout.fillWidth: true
                id: logPath
                text: settings.value("logging/path")
            }

            // Broken!
            // RoundButton {
            //     id: viewLogBtn
            //     text: qsTr("View")
            //     onClicked: {
            //         NaCore.openFile(logPath.text)
            //     }
            // }
        }

        Item {}
        CheckBox {
            id: prune
            text: qsTr("Prune log-file when starting")
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

        ColumnLayout {

            CheckBox {
                id: resync
                text: qsTr("Do a full resynch when\nconnecting to the server")
                checked: settings.value("sync/resync") === "true"
            }

            Button {
                id: resynchNow
                text: qsTr(qsTr("Resynch Now"))
                onClicked: {
                    NaComm.resync()
                    resynchNow.enabled = false
                    resync.enabled = false
                }
            }
        }

        Label {
            text: qsTr("Resend")
        }

        CheckBox {
            id: resend
            text: qsTr("Re-send pending requests")
            checked: settings.value("server/resend_requests") === "true"
        }

        Item{}

        CheckBox {
            id: expertMode
            text: qsTr("Expert mode")
        }

        Label {
            visible: instancesCtl.visible
            text: qsTr("Instances")
        }

        RowLayout {
            id: instancesCtl
            visible: !NaCore.isMobile && expertMode.checked

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
                Layout.leftMargin: 10
                property string singleton: qsTr("Singleton")
                text: instances.value == 1 ? singleton : instances.value.toFixed(0)
            }
        }

        Label {
            visible: expertMode.checked
            text: qsTr("Danger\nZone")
            color: "red"
        }

        RowLayout {
            spacing: 10
            Button {
                text: qsTr("Factory Reset")
                visible: expertMode.checked
                onClicked: {
                    Common.openDialog("settings/ResetConfirmation.qml", root, {})
                }
            }

            Button {
                text: qsTr("Delete Account")
                visible: expertMode.checked
                onClicked: {
                    Common.openDialog("settings/DeleteAccountConfirmation.qml", root, {})
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

}
