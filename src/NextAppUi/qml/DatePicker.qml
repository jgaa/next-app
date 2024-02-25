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
    property bool canSelectMonth: mode === NextappPB.ActionDueType.DATE || mode === NextappPB.ActionDueType.DATETIME

    signal selectedDateClosed(var date, var accepted)

    onDateChanged: {
        grid.year = date.getFullYear()
        grid.month = date.getMonth()
        currentDay = date.getDate()
    }

    onOpened: {
        console.log("DatePicker opened. mode=", mode, "Can select month=", canSelectMonth, " teste=", teste)
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
            locale: grid.locale

            Layout.fillWidth: false
        }

        RowLayout {
            WeekNumberColumn {
                month: grid.month
                year: grid.year
                locale: grid.locale

                Layout.fillWidth: false
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

                    color: today && currentDate ? "orange" :  currentDate ? "yellow" : "white"
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
                                popup.accepted = true

                                if (popup.closeOnSelect) {
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
