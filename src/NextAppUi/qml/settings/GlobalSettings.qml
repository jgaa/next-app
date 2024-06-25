import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

Item {
    id: root
    property bool initialized: false
    anchors.fill: parent

    function commit() {
        // console.log("GlobalSettings: commit")
        var tmp = NaComm.getGlobalSettings()
        tmp.defaultWorkHours.start = NaCore.parseHourMin(from.text)
        tmp.defaultWorkHours.end = NaCore.parseHourMin(to.text)
        tmp.timeZone = timeZone.text
        tmp.region = region.text
        tmp.firstDayOfWeekIsMonday = monday.checked
        tmp.autoStartNextWorkSession = autoStartNextWs.checked

        NaComm.saveGlobalSettings(tmp)
        initialized = false
    }

    onVisibleChanged: {
        if (visible && !initialized) {
            var tmp = NaComm.globalSettings
            from.text = NaCore.toHourMin(tmp.defaultWorkHours.start)
            to.text = NaCore.toHourMin(tmp.defaultWorkHours.end)
            timeZone.text = tmp.timeZone
            region.text = tmp.region
            monday.checked = tmp.firstDayOfWeekIsMonday
            initialized = true;
            autoStartNextWs.checked = tmp.autoStartNextWorkSession
        }
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Work Hours")}

        RowLayout {
            TextField {
                id: from
                placeholderText: qsTr("09:00");
                Layout.preferredWidth: 80
            }

            Item {
                width: 6
            }

            TextField {
                id: to
                placeholderText: qsTr("17:00");
                Layout.preferredWidth: 80
            }
        }

        Label { text: qsTr("Time Zone")}

        TextField {
            id: timeZone
            placeholderText: qsTr("Like: Europe/Berlin");
            Layout.fillWidth: true
        }

        Label { text: qsTr("Region")}

        TextField {
            id: region
            placeholderText: qsTr("Like: Europe");
            Layout.fillWidth: true
        }

        // Label { text: qsTr("Language")}

        // DlgInputField {
        //     id: language
        //     //placeholderText: qsTr("to");
        // }

        Item {}

        CheckBox {
            id: monday
            text: qsTr("First day of week is Monday")
        }

        Item {}

        CheckBox {
            id: autoStartNextWs
            text: qsTr("When a work session is completed, automatically start the next one")
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
