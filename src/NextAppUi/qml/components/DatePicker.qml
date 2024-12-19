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

            DayOfWeekRow {
                id: week
                locale: grid.locale
                Layout.fillWidth: false
                leftPadding: weekNumberCtl.width + 5
            }

            RowLayout {
                WeekNumberColumn {
                    id: weekNumberCtl
                    month: grid.month
                    year: grid.year
                    locale: grid.locale
                    Layout.fillWidth: false

                    delegate: Rectangle {
                        width: weekCtl.width
                        height: weekCtl.height

                        required property int weekNumber
                        property bool selectedWeek: weekNumber === NaCore.weekFromDate(popup.date)
                                                    //Common.getISOWeekNumber(popup.date)
                        property bool sameYear: {
                            if (popup.currentMonth === Calendar.January) {
                                return weekNumber < 50
                            } else if (popup.currentMonth === Calendar.December) {
                                return weekNumber > 10
                            } else {
                                return true
                            }
                        }
                        property bool canSelect: sameYear && popup.mode === NextappPB.ActionDueKind.WEEK

                        color: canSelect ? selectedWeek ? "yellow" : "white" : "#f0f0f0"

                        Text {
                            id: weekCtl
                            text: weekNumber
                            font: weekNumberCtl.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        Rectangle {
                            id: week_hover_shadow
                            anchors.fill: parent
                            opacity: 0
                            color: "Blue"
                        }

                        MouseArea {
                            enabled: canSelect
                            anchors.fill: weekCtl
                            onClicked: {
                                popup.date = NaCore.dateFromWeek(popup.currentYear, weekNumber)
                                popup.currentWeek = weekNumber
                                        //Common.getDateFromISOWeekNumber(popup.currentYear, weekNumber)
                                if (popup.closeOnSelect) {
                                    popup.accepted = true
                                    popup.close()
                                }
                                // console.log("weekNumber ", weekNumber)
                            }

                            hoverEnabled: canSelect
                            onEntered: {
                                week_hover_shadow.opacity = 0.30
                            }
                            onExited: {
                                week_hover_shadow.opacity = 0
                            }
                        }
                    }
                }

                MonthGrid {
                    id: grid
                    // month: popup.currentMonth
                    // year: popup.currentYear

                    Layout.fillWidth: false

                    delegate: Rectangle {
                        id: drect
                        height: dtext.height
                        width: dtext.width
                        required property var model
                        property bool currentDate: model.day === popup.currentDay
                                                     && model.month === grid.month
                                                     && model.year === grid.year
                        property bool inRange: drect.model.month === grid.month && drect.model.year === grid.year
                        property var now: new Date()
                        property bool today: model.day === now.getDate()
                                          && model.month === now.getMonth()
                                          && model.year === now.getFullYear()

                        color:  canSelectMonth && today && currentDate ? "orange" :  canSelectMonth && currentDate ? "yellow" : canSelectMonth ?  "white" : "#f0f0f0"
                        opacity: inRange ? 1 : 0

                        Rectangle {
                            id: hover_shadow
                            anchors.fill: parent
                            opacity: 0
                            color: "Blue"
                        }

                        //color: "white"
                        Text {
                            id: dtext
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            opacity: inRange ? 1 : 0
                            text: drect.model.day
                            font.family: grid.font.family
                            font.pixelSize: grid.font.pixelSize
                            font.bold: drect.today

                            MouseArea {
                                //cursorShape: Qt.PointingHandCursor
                                anchors.fill: parent
                                visible: drect.model.month === grid.month && canSelectMonth

                                hoverEnabled: true
                                onEntered: {
                                    hover_shadow.opacity = 0.20
                                }
                                onExited: {
                                    hover_shadow.opacity = 0
                                }

                                onClicked: {
                                    // console.log("model.month ", model.month, "model.year ", model.year, "model.day ", model.day)
                                    // console.log("popup.currentMonth", popup.currentMonth, "popup.currentYear", popup.currentYear)
                                    // console.log("currentDate", currentDate, "inRange", inRange)

                                    popup.date.setYear(grid.year)
                                    popup.date.setMonth(grid.month)
                                    popup.date.setDate(drect.model.day)

                                    if (popup.closeOnSelect) {
                                        popup.accepted = true
                                        popup.close()
                                    }
                                }
                            }
                        }
                    }// delegate
                }
            }
        } // date picker

        GridLayout {
            id: timeSelector
            Layout.fillHeight: true
            Layout.fillWidth: true
            rowSpacing: 4
            columns: 2
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
                     || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
            property int hours: 0
            property int minutes: 0
            property bool visibleTimeDuration: popup.mode === NextappPB.ActionDueKind.SPAN_HOURS

            // RowLayout {
            //     id: timeSelector
            //     visible: popup.mode === NextappPB.ActionDueKind.DATETIME
            //              || popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
            //     Layout.alignment: Qt.AlignHCenter

            Label {
                text: popup.mode === NextappPB.ActionDueKind.DATETIME ? qsTr("Time") : qsTr("From")
            }

            TimeInput {
                id: timeInput
                Layout.preferredWidth: 80

                valueInSeconds: (popup.date.getHours() * 3600 + popup.date.getMinutes() * 60) + (popup.date.getTimezoneOffset() * 60)
                onTimeChanged: function (hours, minutes) {
                    console.log("timeInput.onTimeChanged: hours=", hours, "minutes=", minutes)
                    popup.date.setHours(hours)
                    popup.date.setMinutes(minutes)
                }
            }


            // RowLayout {
            //     id: durationSelector
            //     visible: popup.mode === NextappPB.ActionDueKind.SPAN_HOURS
            //     Layout.alignment: Qt.AlignHCenter
            //     property int hours: 0
            //     property int minutes: 0

                // Dropdown with "Duration" or "Until"
            ComboBox {
                id: durationCombo
                //Layout.preferredWidth: parent.width / 2
                model: ListModel {
                    ListElement{ text: qsTr("Duration") }
                    ListElement{ text: qsTr("Until") }
                }

                Component.onCompleted: {
                    timeSelector.setDuration(timeSelector.hours, timeSelector.minutes)
                    durationInput.valueInSeconds = timeSelector.getValueInSeconds()
                }
            }

            TimeInput {
                id: durationInput
                Layout.preferredWidth: 80

                valueInSeconds: timeSelector.getValueInSeconds() //(popup.endDate.getHours() * 3600 + popup.endDate.getMinutes() * 60) + (popup.date.getTimezoneOffset() * 60)
                onTimeChanged: function (hours, minutes) {
                    console.log("durationInput.onTimeChanged: hours=", hours, "minutes=", minutes)

                    // Set the endDate based on the value in durationInput and durationCombo
                    timeSelector.hours = hours
                    timeSelector.minutes = minutes
                    timeSelector.setDuration(hours, minutes)
                }
            }

            function getValueInSeconds() {
                if (durationCombo.currentIndex === 0) {
                    return (popup.endDate.getHours() * 3600 + popup.endDate.getMinutes() * 60) + (popup.date.getTimezoneOffset() * 60)
                } else {
                    return (popup.date.getHours() * 3600 + popup.date.getMinutes() * 60) + (popup.date.getTimezoneOffset() * 60)
                }
            }

            function setDuration(hours, minutes) {
                popup.endDate = new Date(popup.date)
                if (durationCombo.currentIndex === 0) {
                    const seconds = hours * 3600 + minutes * 60
                    popup.endDate.setSeconds(popup.date.getSeconds() + seconds)
                } else {
                    // Set the endDate to the end of the day
                    popup.endDate.setHours(hours)
                    popup.endDate.setMinutes(minutes)
                    popup.endDate.setSeconds(0)
                }

                console.log("popup.endDate: ", popup.endDate)
            }

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
        if (mode === NextappPB.ActionDueKind.MONTH) {
            date = new Date(grid.year, grid.month, 1)
        } else if (mode === NextappPB.ActionDueKind.QUARTER) {
            date = new Date(grid.year, grid.month, 1)
        } else if (mode === NextappPB.ActionDueKind.YEAR) {
            date = new Date(grid.year, 0, 1)
        }

        if (mode === NextappPB.ActionDueKind.SPAN_HOURS) {
            selectedDurationClosed(date, endDate, accepted)
        } else {
            selectedDateClosed(date, accepted)
        }

        if (mode === NextappPB.ActionDueKind.WEEK) {
            selectedWeekClosed(date, accepted, currentWeek)
        }
    }
}
