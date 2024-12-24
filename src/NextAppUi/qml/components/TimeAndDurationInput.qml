import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "../common.js" as Common
import Nextapp.Models

GridLayout {
    id: root
    property bool hasDuration: false
    property int startSeconds: 0
    property int durationSeconds: 0
    property bool valid: timeInput.valid && durationInput.valid
    Layout.fillWidth: true
    rowSpacing: 4
    columns: 2

    function setTimeByDate(date) {
        timeInput.setValue(date.getHours() * 3600 + date.getMinutes() * 60)
    }

    function setDurationByDate(date) {
        var when = (date.getHours() * 3600 + date.getMinutes() * 60) - startSeconds
        if (when < 0) {
            when = 0
        }

        //console.log("setDurationByDate date=", date, "when=", when, "startSeconds=", startSeconds)
        durationInput.setValue(when)
    }

    function setTimeInDate(date) {
        var when = new Date(date)
        const hours = startSeconds / 3600
        const minutes = (startSeconds % 3600) / 60
        when.setHours(hours)
        when.setMinutes(minutes)
        when.setSeconds(0)
        when.setMilliseconds(0)
        //console.log("setTimeInDate: when=", when, "hours=", hours, "minutes=", minutes)
        return when
    }

    function addDurationToDate(date) {
        var when = new Date(date.getTime() + durationSeconds * 1000)
        when.setSeconds(0)
        when.setMilliseconds(0)
        //console.log("addDurationToDate: when=", when, "durationSeconds=", durationSeconds)
        return when
    }

    // Start time
    // =============================

    Label {
        text: hasDuration ? qsTr("From") : qsTr("Time")
    }

    TimeInput {
        id: timeInput
        Layout.preferredWidth: 80
        property int when: 0

        onTimeChanged: function (hours, minutes) {
            console.log("timeInput.onTimeChanged: hours=", hours, "minutes=", minutes)
            when = hours * 3600 + minutes * 60
            root.startSeconds = when
            durationInput.syncTime()
        }

        function setValue(value) {
            valueInSeconds = value
        }
    }

    // Duration
    // =============================

    // Dropdown with "Duration" or "Until"
    ComboBox {
        id: durationCombo
        visible: hasDuration
        model: ListModel {
            ListElement{ text: qsTr("Duration") }
            ListElement{ text: qsTr("Until") }
        }

        onCurrentIndexChanged: {
            durationInput.syncTime()
        }
    }

    // Duration, ether a duration span as hours/minutes or a time of day
    // depending on the dropdown above
    TimeInput {
        id: durationInput
        visible: hasDuration
        Layout.preferredWidth: 80
        property int visibleValue: 0 // Whatever is visible to the user, but in seconds

        onTimeChanged: function (hours, minutes) {
            // The signal is triggered weather the control is visible or not
            visibleValue = hours * 3600 + minutes * 60
            root.durationSeconds = getDurationInSeconds()
            //console.log("durationInput.onTimeChanged: hours=", hours, "minutes=", minutes, "duration=", root.durationSeconds)

            const limitedValue = limitDurationToValidValue(root.durationSeconds)
            if (root.durationSeconds !== limitedValue) {
                //console.log("durationInput.onTimeChanged [invalid value]: setting value to ", limitedValue)
                durationInput.setValue(limitedValue)
            }
        }

        function setValue(value) {
            valueInSeconds = value
        }

        function syncTime() {
            var value = limitDurationToValidValue(root.durationSeconds)
            if (durationCombo.currentIndex === 1) {
                value += root.startSeconds
            }
            setValue(value)
        }

        function getDurationInSeconds() {
            if (durationCombo.currentIndex === 0) {
                return visibleValue
            } else {
                return visibleValue - root.startSeconds
            }
        }

        function limitDurationToValidValue(value) {
            const res = Math.min(value + root.startSeconds, /* seconds in a day*/ 86400 - 60)
            return Math.floor(res - root.startSeconds)
        }
    }
}
