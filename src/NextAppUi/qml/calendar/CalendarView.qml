import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi

Rectangle {
    id: root
    Layout.fillHeight: true
    Layout.fillWidth: true
    //Layout.preferredWidth: dayplan.implicitWidth
    //implicitWidth: dayplan.implicitWidth
    //Layout.fillWidth: true
    // height: 800
    // width: 800
    color: MaterialDesignStyling.surface
    property CalendarModel model: CalendarModel

    onVisibleChanged: {
        console.log("Visible changed")
        if (visible) {
            console.log("Visible. model is", model)
            if (!model.valid) {
                model.set(CalendarModel.CM_DAY, 2024, 5, 20)
            }
        }
    }

    RowLayout {
        anchors.fill: parent

        // One day plannin
        ScrollView {
            id: scrollView
            Layout.fillHeight: true
            Layout.fillWidth: true
            //Layout.preferredWidth: dayplan.implicitWidth + 10
            contentWidth: width - 10//dayplan.implicitWidth
            contentHeight: dayplan.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

            Component.onCompleted: {
                scrollView.ScrollBar.vertical.position = 0.25
            }

            DayPlan {
                id: dayplan
                model: root.model.getDayModel(dayplan, 2024, 5, 20)
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
