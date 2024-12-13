import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
//import "common.js" as CommonJS
import Nextapp.Models

Popup {
    id: dusroot

    property NextappPB.due due
    property int maybeDueType: 0
    property int when: 0 // Time as time_t

    padding: 10
    margins: 20
    modal: true
    contentHeight: btn1.height * 8 + 10 * 7
    contentWidth: 300
    closePolicy: Popup.NoAutoClose

    topInset: -2
    leftInset: -2
    rightInset: -6
    bottomInset: -6

    signal selectionChanged(NextappPB.due due)

    onOpened: {
        if (due.hasStart) {
            when = due.start
        }

        if (when < 3600) {
            when = Date.now() / 1000
        }

        maybeDueType = due.kind
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

    ColumnLayout {
        spacing: 0
        anchors.fill: parent

        Button {
            id: btn1
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.DATETIME)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.DATETIME
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            id: btnWinHours
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.SPAN_HOURS)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.SPAN_HOURS
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.DATE)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.DATE
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.SPAN_DAYS)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.SPAN_DAYS
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.WEEK)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.WEEK
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.MONTH)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.MONTH
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.QUARTER)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.QUARTER
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: NaActionsModel.whenListElement(when, dusroot.due.kind, NextappPB.ActionDueKind.YEAR)
            Layout.fillWidth: true
            onClicked: {
                maybeDueType = NextappPB.ActionDueKind.YEAR
                datePicker.mode = maybeDueType
                datePicker.date = new Date(when * 1000)
                datePicker.open()
            }
        }

        Button {
            text: qsTr("No due time")
            Layout.fillWidth: true
            onClicked: {
                due.due = 0
                due.start = 0;
                due.kind = NextappPB.ActionDueKind.UNSET
                selectionChanged(due)
                dusroot.close()
            }
        }

        Button {
            text: qsTr("Cancel")
            Layout.fillWidth: true
            onClicked: {
                dusroot.close()
            }
        }
    }

    DatePicker {
        id: datePicker
        modal: true

        onSelectedDateClosed: (date, accepted) => {
            if (accepted) {
                due = NaActionsModel.adjustDue(date.getTime() / 1000, maybeDueType);
                selectionChanged(due)
            }

            dusroot.close()
        }
    }
}
