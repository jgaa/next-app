import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ComboBox {
    id: root
    signal priorityChanged(int priority)
    currentIndex: -1
    displayText: currentIndex === -1 ?  qsTr("Priority") : currentText

    model: ListModel {
        ListElement{ text: qsTr("Critical")}
        ListElement{ text: qsTr("Very Important")}
        ListElement{ text: qsTr("Higher")}
        ListElement{ text: qsTr("High")}
        ListElement{ text: qsTr("Normal")}
        ListElement{ text: qsTr("Medium")}
        ListElement{ text: qsTr("Low")}
        ListElement{ text: qsTr("Insignificant")}
    }

    onCurrentIndexChanged: {
        console.log("current index changed to", currentIndex)

        if (currentIndex >= 0) {
            console.log("priority changed to", currentIndex)
            const pri = currentIndex
            priorityChanged(pri)
        }
    }

    function reset() {
        currentIndex = -1
    }
}
