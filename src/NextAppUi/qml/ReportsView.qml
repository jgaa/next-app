import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import NextAppUi

Rectangle {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    color: MaterialDesignStyling.surface

    ColumnLayout {
        anchors.fill: parent
        TabBar {
            id: bar
            Layout.fillWidth: true
            StyledTabButton {
                text: qsTr("Week")
            }
            StyledTabButton {
                text: qsTr("Today")
            }
            StyledTabButton {
                text: qsTr("Month")
            }

            background: Rectangle {
                implicitHeight: 40
                color: MaterialDesignStyling.primary
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
