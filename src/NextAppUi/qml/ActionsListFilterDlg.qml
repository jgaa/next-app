import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as Common
import Nextapp.Models


Popup {
    id: popup

    property var flags
    property alias unscheduled: unscheduled.isChecked
    signal apply()

    padding: 10
    margins: 20
    modal: true
    width: 250
    height: 180

    onVisibleChanged: {
        if (visible) {
            flags = ActionsModel.flags
            done.isChecked = flags.done
            active.isChecked = flags.active
            unscheduled.isChecked = flags.unscheduled
            //upcoming.isChecked = flags.upcoming
        }
    }

    background: Rectangle {
        color: "white"
        radius: 5

        Rectangle {
            anchors.fill: parent
            color: "#f0f0f0"
            radius: 5
            border.color: "#d0d0d0"
            border.width: 1
        }
    }

    contentItem: ColumnLayout {
        id: content
        spacing: 5

        CheckBoxWithFontIcon {
            id: done
            text: qsTr("Show done actions")
        }

        CheckBoxWithFontIcon {
            id: active
            text: qsTr("Show active actions")
        }

        CheckBoxWithFontIcon {
            id: unscheduled
            text: qsTr("Show unscheduled actions")
        }

        // CheckBoxWithFontIcon {
        //     id: upcoming
        //     text: qsTr("Show upcoming actions")
        // }

        Button {
            text: qsTr("Apply")
            onClicked: {
                popup.flags.active = active.isChecked
                popup.flags.done = done.isChecked
                popup.flags.unscheduled = unscheduled.isChecked
                //popup.flags.upcoming = upcoming.isChecked

                ActionsModel.flags = flags
                popup.close()
            }
        }
    }
}
