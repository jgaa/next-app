import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as Common

Popup {
    id: popup

    padding: 10
    margins: 20
    //modal: true
    // contentHeight: content.implicitHeight
    // contentWidth: content.implicitWidth
    width: 300
    height: 400

    property int mode: NextappPB.ActionDueType.UNSET
    property var date: new Date()
    property bool accepted: false
    property alias currentYear: grid.year
    property alias currentMonth: grid.month
    property int currentDay

    property bool closeOnSelect: mode === NextappPB.ActionDueType.DATE
                                 || mode === NextappPB.ActionDueType.WEEK
    property bool canSelectMonth: mode === NextappPB.ActionDueType.DATE
                                  || mode === NextappPB.ActionDueType.DATETIME

    signal selectedDateClosed(var date, var accepted)

    onDateChanged: {
        grid.year = date.getFullYear()
        grid.month = date.getMonth()
        currentDay = date.getDate()
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
            font.pointSize: font.pointSize * 1.5
            Layout.alignment: Qt.AlignHCenter
            text: {
                switch(popup.mode) {
                    case NextappPB.ActionDueType.DATE:
                        return qsTr("Select a date")
                    case NextappPB.ActionDueType.WEEK:
                        return qsTr("Select a week")
                    case NextappPB.ActionDueType.DATETIME:
                        return qsTr("Select a date and time")
                    case NextappPB.ActionDueType.QUARTER:
                        return qsTr("Select a quarter")
                    case NextappPB.ActionDueType.MONTH:
                        return qsTr("Select a month")
                    case NextappPB.ActionDueType.YEAR:
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
                    property bool selectedWeek: weekNumber === Common.getISOWeekNumber(popup.date)
                    property bool sameYear: {
                        if (popup.currentMonth === Calendar.January) {
                            return weekNumber < 50
                        } else if (popup.currentMonth === Calendar.December) {
                            return weekNumber > 10
                        } else {
                            return true
                        }
                    }
                    property bool canSelect: sameYear && popup.mode === NextappPB.ActionDueType.WEEK

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
                            popup.date = Common.getDateFromISOWeekNumber(popup.currentYear, weekNumber)
                            if (popup.closeOnSelect) {
                                popup.accepted = true
                                popup.close()
                            }
                            console.log("weekNumber ", weekNumber)
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
                                console.log("model.month ", model.month, "model.year ", model.year, "model.day ", model.day)
                                console.log("popup.currentMonth", popup.currentMonth, "popup.currentYear", popup.currentYear)
                                console.log("currentDate", currentDate, "inRange", inRange)

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

        RowLayout {
            id: timeSelector
            visible: popup.mode === NextappPB.ActionDueType.DATETIME
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
            spacing: 6
            text: qsTr("Today")
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
                //visible: !popup.closeOnSelect
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
        selectedDateClosed(date, accepted)
    }
}
