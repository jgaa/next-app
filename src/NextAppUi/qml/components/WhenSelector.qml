import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
//import "common.js" as Common
import Nextapp.Models

ComboBox {
    id: root
    property var due: null
    property var maybeKind: NextappPb.ActionDueKind.UNSET
    displayText: NaActionsModel.formatDue(due)
    signal dueWasSelected(var due)

    model: ListModel {
        ListElement{ text: qsTr("DateTime")}
        ListElement{ text: qsTr("Date")}
        ListElement{ text: qsTr("Week")}
        ListElement{ text: qsTr("Month")}
        ListElement{ text: qsTr("Quarter")}
        ListElement{ text: qsTr("Year")}
        ListElement{ text: qsTr("Unset")}
        ListElement{ text: qsTr("Span Hours")}
        ListElement{ text: qsTr("Span Days")}
    }

    contentItem: RowLayout {
        spacing: 5
        anchors.fill: parent
        anchors.margins: 4

        Image {
            source: "../../icons/fontawsome/calendar.svg"
            sourceSize.width: 20
            sourceSize.height: 20
            fillMode: Image.PreserveAspectFit
        }

        Text {
            text: root.displayText
        }

        Item {
            Layout.fillWidth: true
        }
    }

    Component.onCompleted: {
        // Connect to the popup's onVisibleChanged signal
        root.popup.visibleChanged.connect(function() {
            if (!root.popup.visible) {
                root.maybeKind = currentIndex
                const  when = (due && due.start > 3600) ? due.start : Date.now() / 1000
                const until = (due && due.due > 3600) ? due.due : Date.now() / 1000

                switch(currentIndex) {
                    case NextappPb.ActionDueKind.DATETIME:
                    case NextappPb.ActionDueKind.DATE:
                    case NextappPb.ActionDueKind.WEEK:
                    case NextappPb.ActionDueKind.MONTH:
                    case NextappPb.ActionDueKind.QUARTER:
                    case NextappPb.ActionDueKind.YEAR:
                    case NextappPb.ActionDueKind.SPAN_HOURS:
                    case NextappPb.ActionDueKind.SPAN_DAYS:
                        datePicker.mode = root.maybeKind
                        datePicker.date = new Date(when * 1000)
                        datePicker.endDate = new Date(until * 1000)
                        datePicker.open()
                        break;
                    case NextappPb.ActionDueKind.UNSET:
                        if (!due) {
                            // construct a default due
                            due = NaActionsModel.getEmptyDue()
                        }

                        due.due = 0
                        due.start = 0;
                        dueWasSelected(root.due)
                        break;
                }

                displayText = NaActionsModel.formatDue(due)
            }
        });
    }

    DatePicker {
        id: datePicker
        modal: true
        visible: false

        onSelectedDateClosed: (date, accepted) => {
            if (accepted) {
                root.due = NaActionsModel.adjustDue(date.getTime() / 1000, root.maybeKind);
                setWhenCurrentIndex(root.due.kind)
                dueWasSelected(root.due)
            }
        }

        onSelectedDurationClosed: (from, until, accepted) => {
            if (accepted) {
                root.due = NaActionsModel.setDue(from.getTime() / 1000, until.getTime() / 1000, root.maybeKind);
                setWhenCurrentIndex(root.due.kind)
                dueWasSelected(root.due)
            }
        }
    }

    // Set the current index and the text
    function setWhenCurrentIndex(index) {
        root.currentIndex = index
        root.displayText = NaActionsModel.formatDue(root.due)
    }

    function reset() {
        root.currentIndex  = -1;
    }
}
