import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: difficultyCtl
    signal difficultyChanged(int difficulty)
    displayText: currentIndex === -1 ?  qsTr("Difficulty") : currentText

    model: ListModel {
        ListElement{ text: qsTr("Trivial")}
        ListElement{ text: qsTr("Easy")}
        ListElement{ text: qsTr("Normal")}
        ListElement{ text: qsTr("Hard")}
        ListElement{ text: qsTr("Very Hard")}
        ListElement{ text: qsTr("Inspiered moment")}
    }

    onCurrentIndexChanged: {
        if (currentIndex === -1) return

        difficultyCtl.difficultyChanged(currentIndex)
    }

    function reset() {
        currentIndex = -1
    }
}
