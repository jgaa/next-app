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

    function commit() {
        console.log("GlobalSettings: commit")
        var tmp = NaComm.getGlobalSettings()
        tmp.defaultWorkHours.start = NaCore.parseHourMin(from.text)
        tmp.defaultWorkHours.end = NaCore.parseHourMin(to.text)
        tmp.timeZone = timeZone.text
        tmp.region = region.text
        tmp.firstDayOfWeekIsMonday = monday.isChecked
        tmp.autoStartNextWorkSession = autoStartNextWs.isChecked

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
            monday.isChecked = tmp.firstDayOfWeekIsMonday
            initialized = true;
            autoStartNextWs.isChecked = tmp.autoStartNextWorkSession
        }
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Work Hours")}

        RowLayout {
            DlgInputField {
                id: from
                //placeholderText: qsTr("from");
                Layout.preferredWidth: 80
            }

            Item {
                width: 6
            }

            DlgInputField {
                id: to
                //placeholderText: qsTr("to");
                Layout.preferredWidth: 80
            }
        }

        Label { text: qsTr("Time Zone")}

        DlgInputField {
            id: timeZone
            //placeholderText: qsTr("to");
            Layout.fillWidth: true
        }

        Label { text: qsTr("Region")}

        DlgInputField {
            id: region
            //placeholderText: qsTr("to");
            Layout.fillWidth: true
        }

        // Label { text: qsTr("Language")}

        // DlgInputField {
        //     id: language
        //     //placeholderText: qsTr("to");
        // }

        Item {}

        CheckBoxWithFontIcon {
            id: monday
            text: qsTr("First day of week is Monday")
            textColor: "black"

            //placeholderText: qsTr("to");
        }

        Item {}

        CheckBoxWithFontIcon {
            id: autoStartNextWs
            text: qsTr("When a work session is completed, automatically start the next one")
            textColor: "black"

            //placeholderText: qsTr("to");
        }
    }
}
