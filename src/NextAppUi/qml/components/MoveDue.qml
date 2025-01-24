import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import Nextapp.Models

ComboBox {
    id: root
    property var due: NaActionsModel.getEmptyDue()
    signal dueValueChanged(var due)

    displayText: qsTr("Move the due time")
    currentIndex: -1
    model: ListModel {
        ListElement{ text: qsTr("Today")}
        ListElement{ text: qsTr("Tomorrow")}
        ListElement{ text: qsTr("This Weekend")}
        ListElement{ text: qsTr("Next Monday")}
        ListElement{ text: qsTr("This week")}
        ListElement{ text: qsTr("After one week")}
        ListElement{ text: qsTr("Next Week")}
        ListElement{ text: qsTr("This month")}
        ListElement{ text: qsTr("Next month")}
        ListElement{ text: qsTr("This Quarter")}
        ListElement{ text: qsTr("Next Quarter")}
        ListElement{ text: qsTr("This Year")}
        ListElement{ text: qsTr("Next Year")}
        ListElement{ text: qsTr("-- Cancel --")}
    }

    onCurrentIndexChanged: {
        if (currentIndex >= 0 && currentIndex < 13) {
            root.due = NaActionsModel.changeDue(currentIndex, root.due)
            dueValueChanged(root.due)
        }
    }

    function reset() {
        currentIndex = -1
    }
}
