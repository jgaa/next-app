import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "../common.js" as Common
import Nextapp.Models

/* Selects a time.
  The selected time is the start of the unit, based on the ActionDueKind.
  For example, if the ActionDueKind is WEEK, the selected time is the start of that week.

  The time is based on the local time zone, so if the user later viewe the events in a different
  time-zone, the time may be unaligned. For example, the original datem wee or month etc. may be different.
 */
Popup {
    id: popup

    padding: 10
    margins: 20
    //modal: true
    // contentHeight: content.implicitHeight
    // contentWidth: content.implicitWidth
    width: 300
    height: 400

    property int mode: NextappPB.ActionDueKind.UNSET
    property var date: new Date()
    property bool accepted: false
    property alias currentYear: grid.year
    property alias currentMonth: grid.month
    property int currentDay
    property int currentWeek: 0
    property var endDate: null
    // A separate one-day visual marker for callers that need to call attention
    // to a date without changing the selected value.
    property date highlightedDate: new Date(NaN)

    property bool closeOnSelect: mode === NextappPB.ActionDueKind.DATE
                                 || mode === NextappPB.ActionDueKind.WEEK
    property bool canSelectMonth: mode === NextappPB.ActionDueKind.DATE
                                  || mode === NextappPB.ActionDueKind.DATETIME
                                  || mode === NextappPB.ActionDueKind.SPAN_HOURS
                                  || mode === NextappPB.ActionDueKind.SPAN_DAYS

    signal selectedDateClosed(var date, var accepted)
    signal selectedDurationClosed(var start, var until, var accepted)
    signal selectedWeekClosed(var date, var accepted, var week)

    onDateChanged: {
        // console.log("DatePicker.onDateChanged: date=", date.toISOString())
        grid.year = date.getFullYear()
        grid.month = date.getMonth()
        currentDay = date.getDate()
        quarterCombo.currentIndex = Math.floor(grid.month / 3)
        timeSelector.setTimeByDate(date)
    }

    onEndDateChanged: {
        console.log("DatePicker.onEndDateChanged: endDate=", endDate)
        if (endDate !== null) {
            timeSelector.setDurationByDate(endDate)
        } else {
            timeSelector.durationSeconds = 0
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
        //anchors.fill: parent
        spacing: 5

        Text {
            font.bold: true

            Component.onCompleted: {
                font.pointSize = font.pointSize * 1.5
            }

            Layout.alignment: Qt.AlignHCenter
            text: {
                switch(popup.mode) {
                    case NextappPB.ActionDueKind.DATE:
                        return qsTr("Select a date")
                    case NextappPB.ActionDueKind.WEEK:
                        return qsTr("Select a week")
                    case NextappPB.ActionDueKind.DATETIME:
                        return qsTr("Select a date and time")
                    case NextappPB.ActionDueKind.QUARTER:
                        return qsTr("Select a quarter")
                    case NextappPB.ActionDueKind.MONTH:
                        return qsTr("Select a month")
                    case NextappPB.ActionDueKind.YEAR:
                        return qsTr("Select a year")
                    case NextappPB.ActionDueKind.SPAN_HOURS:
                        return qsTr("Select a date and time-span")
                    case NextappPB.ActionDueKind.SPAN_DAYS:
                        return qsTr("Select a date-span")
                    default:
                        return qsTr("Select a date")
                }
            }
        }

        GridLayout {
            //id: grid
            Layout.fillHeight: true
            Layout.fillWidth: true
            rowSpacing: 4
            property int controls : popup.mode === NextappPB.ActionDueKind.DATETIME
                                    || popup.mode === NextappPB.ActionDueKind.DATE
                                    || popup.mode === NextappPB.ActionDueKind.MONTH
                                    || popup.mode === NextappPB.ActionDueKind.WEEK
                                    || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
                                    || popup.mode === NextappPB.ActionDueKind.SPAN_DAYS
                                    || popup.mode == NextappPB.ActionDueKind.QUARTER ? 2 : 1

            columns: NaCore.isMobile ? 1 : controls

            // Year Selector
            SpinBox {
                editable: true
                id: yearSpinner
                from: popup.date.getFullYear() - 5
                to: new Date().getFullYear() + 15
                value: popup.currentYear
                onValueChanged: {
                    grid.year = value
                }
            }

            // Month Selector
            ComboBox {
                id: monthCombo

                visible: popup.mode === NextappPB.ActionDueKind.DATETIME
                        || popup.mode === NextappPB.ActionDueKind.DATE
                        || popup.mode === NextappPB.ActionDueKind.MONTH
                        || popup.mode === NextappPB.ActionDueKind.WEEK
                        || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
                        || popup.mode === NextappPB.ActionDueKind.SPAN_DAYS

                model: ListModel {
                    ListElement{ text: qsTr("January") }
                    ListElement{ text: qsTr("February") }
                    ListElement{ text: qsTr("March") }
                    ListElement{ text: qsTr("April") }
                    ListElement{ text: qsTr("May") }
                    ListElement{ text: qsTr("June") }
                    ListElement{ text: qsTr("July") }
                    ListElement{ text: qsTr("August") }
                    ListElement{ text: qsTr("September") }
                    ListElement{ text: qsTr("October") }
                    ListElement{ text: qsTr("November") }
                    ListElement{ text: qsTr("December") }
                }

                currentIndex: popup.currentMonth
                onCurrentIndexChanged: {
                    grid.month = currentIndex
                }
            }

            ComboBox {
                id: quarterCombo
                visible: mode === NextappPB.ActionDueKind.QUARTER
                model: ListModel {
                    ListElement{ text: qsTr("Q1")}
                    ListElement{ text: qsTr("Q2") }
                    ListElement{ text: qsTr("Q3") }
                    ListElement{ text: qsTr("Q4") }
                }

                onCurrentIndexChanged: {
                    var mapping = [0, 3, 6, 9]
                    grid.month = mapping[currentIndex]
                }
            }
        }

        ColumnLayout {
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
                    || popup.mode === NextappPB.ActionDueKind.DATE
                    || popup.mode === NextappPB.ActionDueKind.WEEK
                    || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
                    || popup.mode === NextappPB.ActionDueKind.SPAN_DAYS

            DateCalendar {
                id: grid
                Layout.alignment: Qt.AlignHCenter
                year: popup.date.getFullYear()
                month: popup.date.getMonth()
                firstDayOfWeek: NaComm.globalSettings.firstDayOfWeekIsMonday ? Qt.Monday : Qt.Sunday
                selectedDate: popup.date
                selectionIsWeek: popup.mode === NextappPB.ActionDueKind.WEEK
                highlightedDate: popup.highlightedDate
                interactive: popup.canSelectMonth
                weekNumbersInteractive: popup.mode === NextappPB.ActionDueKind.WEEK
                todayBackground: "lightgreen"
                // The picker owns a white popup background, regardless of the
                // application theme, so its normal text must remain dark.
                normalDayTextColor: "#1D1B20"
                weekNumberColor: "#49454F"
                selectedWeekBackground: "#E8DEF8"
                weekModeDayTextColor: "#4A4458"

                onDateActivated: function(selected) {
                    var d = new Date(popup.date) // retain the selected time
                    d.setFullYear(selected.getFullYear(), selected.getMonth(), selected.getDate())
                    popup.date = d
                    if (popup.closeOnSelect) {
                        popup.accepted = true
                        popup.close()
                    }
                }

                onWeekActivated: function(weekStart, weekNumber) {
                    popup.date = weekStart
                    popup.currentWeek = weekNumber
                    if (popup.closeOnSelect) {
                        popup.accepted = true
                        popup.close()
                    }
                }
            }
        } // date picker

        RowLayout {
            id: daysSelector
            //Layout.fillHeight: true
            Layout.fillWidth: true
            visible: popup.mode === NextappPB.ActionDueKind.SPAN_DAYS

            Label {
                text: qsTr("Days")
            }

            // Input that only accept positive digits and emits a signal when the value changes
            TextField {
                id: daysInput
                Layout.preferredWidth: 80
                validator: IntValidator { bottom: 1; top: 9999 }

                property bool programmaticChange: false

                onTextChanged: {
                    if (!programmaticChange && daysSelector.visible && text !== "") {
                        const seconds_to_add = parseInt(text) * 24 * 60 * 60;
                        var when = new Date(popup.date);
                        when.setSeconds(when.getSeconds() + seconds_to_add);
                        popup.endDate = when
                        console.log("daysSelector: Setting endDate to ", when.toISOString());
                    }
                }

                function setValue(value) {
                    programmaticChange = true; // Set flag to prevent onTextChanged
                    text = value;              // Change the value
                    programmaticChange = false; // Reset flag
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        TimeAndDurationInput {
            id: timeSelector
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
                     || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
            hasDuration: popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
        }

        Button {
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
            spacing: 6
            text: qsTr("Now")
            onClicked: {
                popup.date = new Date()
                popup.date.setSeconds(0)
                popup.accepted = true
                popup.close()
            }
        }

        RowLayout {
            Button {
                spacing: 6
                text: qsTr("OK")
                enabled: timeSelector.valid
                onClicked: {
                    popup.accepted = true
                    popup.close()
                }
            }

            Button {
                spacing: 6
                text: qsTr("Cancel")
                onClicked: {
                    popup.accepted = false
                    popup.close()
                }
            }
        }

        Item {
            Layout.fillHeight:  true
        }
    }

    onClosed: {
        switch(mode) {
            case NextappPB.ActionDueKind.DATE:
                selectedDateClosed(date, accepted)
                break
            case NextappPB.ActionDueKind.WEEK:
                selectedDateClosed(date, accepted)
                selectedWeekClosed(date, accepted, currentWeek)
                //console.log("selectedWeekClosed: ", date, accepted, currentWeek)
                break
            case NextappPB.ActionDueKind.DATETIME: {
                const when = timeSelector.setTimeInDate(date)
                selectedDateClosed(when, accepted)
                }
                break
            case NextappPB.ActionDueKind.QUARTER: {
                const when = new Date(grid.year, grid.month, 1)
                selectedDateClosed(when, accepted)
                }
                break
            case NextappPB.ActionDueKind.MONTH: {
                const when = new Date(grid.year, grid.month, 1)
                selectedDateClosed(when, accepted)
                }
                break
            case NextappPB.ActionDueKind.YEAR: {
                const when = new Date(grid.year, 0, 1)
                selectedDateClosed(when, accepted)
                }
                break
            case NextappPB.ActionDueKind.SPAN_HOURS: {
                const start = timeSelector.setTimeInDate(date)
                const until = timeSelector.addDurationToDate(start)
                selectedDurationClosed(start, until, accepted)
                }
                break
            case NextappPB.ActionDueKind.SPAN_DAYS:
                selectedDurationClosed(date, endDate || date, accepted)
                break
            default:
                console.log("DatePicker: **** Unhandled mode ****: ", mode)
        }
    }
}
