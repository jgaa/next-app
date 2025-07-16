import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb


ScrollView {
    id: root
    anchors.fill: parent

    Settings {
        id: settings
    }

    function commit() {
        settings.setValue("alarms/calendarEvent.enabled", alarmsEnabled.checked ? "true" : "false")
        settings.setValue("alarms/calendarEvent.SoonToStartMinutes", alarmsBeforeStart.value)
        settings.setValue("alarms/calendarEvent.SoonToEndMinutes", alarmsBeforeEnd.value)
        settings.setValue("alarms/calendarEvent.volume", alarmsVolume.value)
        if (NaCore.hasPushNotifications) {
            settings.setValue("push/updates", pushUpdatesEnabled.checked ? "true" : "false")
        }
        settings.sync()
    }

    ColumnLayout {
        width: parent.width

        Label { text: qsTr("Calendar Alarms")}

        GridLayout {
            Layout.leftMargin: 20
            Layout.fillWidth: true
            rowSpacing: 4
            columns: 2

            Label { text: qsTr("Alarms")}
            Switch {
                id: alarmsEnabled
                checked: settings.value("alarms/calendarEvent.enabled", "true") === "true"
                text: qsTr("Enabled")
            }

            Label { text: qsTr("Before start")}
            RowLayout {
                enabled: alarmsEnabled.checked
                SpinBox {
                    id: alarmsBeforeStart
                    value: settings.value("alarms/calendarEvent.SoonToStartMinutes", 1)
                }
                Label { text: qsTr("minutes")}
            }

            Label { text: qsTr("Before End")}
            RowLayout {
                enabled: alarmsEnabled.checked
                SpinBox {
                    enabled: alarmsEnabled.checked
                    id: alarmsBeforeEnd
                    value: settings.value("alarms/calendarEvent.SoonToEndMinutes", 5)
                }
                Label { text: qsTr("minutes")}
            }

            Label { text: qsTr("Volume")}
            Slider {
                id: alarmsVolume
                enabled: alarmsEnabled.checked
                from: 0.0
                to: 1.0
                value: settings.value("alarms/calendarEvent.volume", 0.5)

                onMoved: {
                    // Used delayed play to avoid sound being cut off by the next sound during sliding
                    NaCore.playSoundDelayed(400, alarmsVolume.value, "")
                }
            }
        }

        Label {
            id: pushNotificationsLabel
            enabled: NaCore.hasPushNotifications
            text: qsTr("Push notifications")
        }

        GridLayout {
            enabled: pushNotificationsLabel.enabled
            Layout.leftMargin: 20
            Layout.fillWidth: true
            rowSpacing: 4
            columns: 2

            Label {
                text: qsTr("Important updates")
            }

            Switch {
                id: pushUpdatesEnabled
                checked: settings.value("push/updates", "true") === "true"
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

}
