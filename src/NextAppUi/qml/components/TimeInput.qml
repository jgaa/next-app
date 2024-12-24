import QtQuick 2.15
import QtQuick.Controls 2.15
import Nextapp.Models

TextField {
    property int valueInSeconds: 0 // Value in seconds
    property int maxValue: /* secondfs in a day */ 86400 -60
    property bool valid: true
    opacity: 1
    signal timeChanged(int hours, int minutes)
    placeholderText: "hh:mm"
    inputMask: "00:00"
    cursorVisible: true
    inputMethodHints: Qt.ImhTime
    text: NaCore.toHourMin(valueInSeconds)

    function setInputValue(seconds) {
        valueInSeconds = seconds
        text: NaCore.toHourMin(valueInSeconds)
    }

    onTextChanged: {
        // when is seconds
        const isValid = /^\d{2}:\d{2}$/.test(text);
        if (!isValid) {
            valid = false
            return;
        }
        const when = NaCore.parseTime(text)
        console.log("onTextChanged: ", text, " when: ", when)
        if (when !== -1 && when <= maxValue) {
            const hours = Math.floor(when / 3600)
            const minutes = Math.floor((when % 3600) / 60)
            //console.log("TimeInput: emitting signal timeChanged: ", hours, ":", minutes)
            timeChanged(hours, minutes)
            valid = true
        } else {
            valid = false
        }
    }

    color: valid ? "green" : "red"

    validator: RegularExpressionValidator { regularExpression: /^[0-9]*$/ }

    onActiveFocusChanged: {
        if (activeFocus) {
            selectAll(); // Select all text when the TextField gains focus
        }
    }
}
