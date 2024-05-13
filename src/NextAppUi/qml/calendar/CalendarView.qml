import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi

Rectangle {
    Layout.fillHeight: true
    Layout.preferredWidth: dayplan.implicitWidth
    //implicitWidth: dayplan.implicitWidth
    //Layout.fillWidth: true
    // height: 800
    // width: 800
    color: MaterialDesignStyling.surface

    RowLayout {
        anchors.fill: parent

        // One day plannin
        ScrollView {
            id: scrollView
            Layout.fillHeight: true
            Layout.preferredWidth: dayplan.implicitWidth + 10
            contentWidth: dayplan.implicitWidth
            contentHeight: dayplan.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

            Component.onCompleted: {
                scrollView.ScrollBar.vertical.position = 0.26
            }

            DayPlan {
                id: dayplan

            }
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
