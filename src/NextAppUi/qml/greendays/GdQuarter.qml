import QtQuick
import QtQuick.Layouts
import NextAppUi

Item {
    property alias text: qtext.text

    Layout.topMargin: 10
    Layout.fillWidth: true
    Layout.preferredHeight: 30

    Rectangle {
        anchors.fill: parent
        color: MaterialDesignStyling.secondaryContainer
        radius: 5
    }

    Text {
        id: qtext
        anchors.centerIn: parent
        color: MaterialDesignStyling.onSecondaryContainer
    }
}
