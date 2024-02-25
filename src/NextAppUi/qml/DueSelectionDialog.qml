import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as CommonJS

Popup {
    id: popup
    padding: 10
    margins: 20
    modal: true
    contentHeight: btn1.height * 8 + 10 * 7
    contentWidth: 300

    topInset: -2
    leftInset: -2
    rightInset: -6
    bottomInset: -6

    property int when: 0
    property int dueType: 0
    property int maybeDueType: 0
    signal selectionChanged(int when, int dueType)

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

    ColumnLayout {
        spacing: 10
        anchors.fill: parent

        Button {
            id: btn1
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.DATETIME)
            Layout.fillWidth: true
            onClicked: {
                if (!when) {
                    when = Date.now() / 1000
                }
                maybeDueType = NextappPB.ActionDueType.DATETIME
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.DATE)
            Layout.fillWidth: true
            onClicked: {
                if (!when) {
                    when = Date.now() / 1000
                }

                maybeDueType = NextappPB.ActionDueType.DATE
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.WEEK)
            Layout.fillWidth: true
            onClicked: {
                picker.open()
                when = Date.now() / 1000
                dueType = NextappPB.ActionDueType.WEEK
                selectionChanged(when, dueType)
                popup.close()
            }
        }

        Button {
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.MONTH)
            Layout.fillWidth: true
            onClicked: {
                when = Date.now() / 1000
                dueType = NextappPB.ActionDueType.MONTH
                selectionChanged(when, dueType)
                popup.close()
            }
        }

        Button {
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.QUARTER)
            Layout.fillWidth: true
            onClicked: {
                when = Date.now() / 1000
                dueType = NextappPB.ActionDueType.QUARTER
                selectionChanged(when, dueType)
                popup.close()
            }
        }

        Button {
            text: ActionsModel.whenListElement(when, dueType, NextappPB.ActionDueType.YEAR)
            Layout.fillWidth: true
            onClicked: {
                when = Date.now() / 1000
                dueType = NextappPB.ActionDueType.YEAR
                selectionChanged(when, dueType)
                popup.close()
            }
        }

        Button {
            text: qsTr("No due time")
            Layout.fillWidth: true
            onClicked: {
                selectionChanged(0,  NextappPB.ActionDueType.UNSET)
                popup.close()
            }
        }

        Button {
            text: qsTr("Cancel")
            Layout.fillWidth: true
            onClicked: {
                popup.close()
            }
        }
    }


    DatePicker {
        id: datePicker
        modal: true

        onSelectedDateClosed: (date, accepted) => {
            if (accepted) {
                when = date.getTime() / 1000
                dueType = maybeDueType
                selectionChanged(when, dueType)

            }

            popup.close()
        }
    }
}
