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
                property string singleton: qsTr("Singleton")
                Layout.preferredWidth: textMetrics.boundingRect(text).width + 10 // Add some padding
                text: instances.value == 1 ? singleton : instances.value.toFixed(0)
            }
        }

        Label {
            visible: expertMode.checked
            text: qsTr("Danger\nZone")
            color: "red"
        }

        Button {
            text: qsTr("Factory Reset")
            visible: expertMode.checked
            onClicked: {
               resetDialog.open()
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    Dialog {
        id: resetDialog
        title: qsTr("Factory Reset")
        standardButtons: Dialog.Yes | Dialog.No
        width: 400
        height: 300

        contentItem: ColumnLayout {
            spacing: 10

            // ScrollView to allow scrolling long messages
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                TextArea {
                    id: errorMessage
                    text: qsTr("Are you sure you want to reset NextApp?\nThis will open the Signup wizard next time you start NextApp. Your current local configuration and settings will be lost. You will have to re-add this device using an One Time Password (OTP) from another device, or sign up for a new account.")
                    wrapMode: TextArea.Wrap
                    readOnly: true
                    selectByMouse: true  // Allow selecting text for copying
                    background: Rectangle {
                        color: "transparent"  // Make it blend with the dialog
                    }
                }
            }
        }


        onAccepted: {
            settings.setValue("onboarding", false);
            Qt.callLater(Qt.quit)
        }
    }
}
