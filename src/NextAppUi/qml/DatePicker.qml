import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as Common
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

    property bool closeOnSelect: mode === NextappPB.ActionDueKind.DATE
                                 || mode === NextappPB.ActionDueKind.WEEK
    property bool canSelectMonth: mode === NextappPB.ActionDueKind.DATE
                                  || mode === NextappPB.ActionDueKind.DATETIME

    signal selectedDateClosed(var date, var accepted)
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
                    default:
                        return qsTr("Select a date")
                }
            }
        }

        // Year Selector
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Label {text: qsTr("Year")}
            SpinBox {
                editable: true
                id: yearSpinner
                from: popup.date.getFullYear() - 5
                to: new Date().getFullYear() + 15
                value: popup.currentYear
                onValueChanged: {
                    //popup.currentYear = value
                   // popup.date.setYear(value)
                    grid.year = value
                }
            }
        }

        // Month Selector
        RowLayout {
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
                    || popup.mode === NextappPB.ActionDueKind.DATE
                    || popup.mode === NextappPB.ActionDueKind.MONTH
                    || popup.mode === NextappPB.ActionDueKind.WEEK

            Layout.alignment: Qt.AlignHCenter
            Label {text: qsTr("Month")}

            ComboBox {
                id: monthCombo
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
        }

        // Quarter Selector
        RowLayout {
            visible: mode === NextappPB.ActionDueKind.QUARTER
            Layout.alignment: Qt.AlignHCenter
            Label {text: qsTr("Quarter")}

            ComboBox {
                id: quarterCombo
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

        RowLayout {
            id: timeSelector
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
            Layout.alignment: Qt.AlignHCenter

            SpinBox {
                id: hourSpinner
                from: 0
                to: 23
                value: popup.date.getHours()
                onValueChanged: {
                    popup.date.setHours(value)
                }
            }

            SpinBox {
                id: minuteSpinner
                from: 0
                to: 55
                stepSize: 5
                value: (popup.date.getMinutes() / 5) * 5
                onValueChanged: {
                    popup.date.setMinutes(value)
                }
            }
        }

        Button {
            visible: popup.mode === NextappPB.ActionDueKind.DATETIME
            spacing: 6
            text: qsTr("Now")
            onClicked: {
                popup.date = new Date()
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

        selectedDateClosed(date, accepted)

        if (mode === NextappPB.ActionDueKind.WEEK) {
            selectedWeekClosed(date, accepted, currentWeek)
        }
    }
}
