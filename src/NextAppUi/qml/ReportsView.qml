import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import NextAppUi

Rectangle {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    color: "orange"

    ColumnLayout {
        anchors.fill: parent
        TabBar {
            id: bar
            Layout.fillWidth: true
            TabButton {
                text: qsTr("Week")
            }
            TabButton {
                text: qsTr("Today")
            }
            TabButton {
                text: qsTr("Month")
            }
        }

        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: bar.currentIndex

            WeeklyHoursView {
                id: weekly
            }

            Rectangle {
                color: "pink"
            }

            Rectangle {
                color: "red"
            }
        }
    }
}
