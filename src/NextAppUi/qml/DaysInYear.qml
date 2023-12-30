import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi


Rectangle {
    id: root

    property int fontSize: 16

    color: Colors.background

    GridLayout {
        columns: 4
        columnSpacing: 20
        rowSpacing: 10

        Quarter {
            text: "Q1"
        }

        Quarter {
            text: "Q2"
        }

        Quarter {
            text: "Q3"
        }

        Quarter {
            text: "Q4"
        }

        Month {
            month: Calendar.January
        }

        Month {
            month: Calendar.April
        }

        Month {
            month: Calendar.July
        }

        Month {
            month: Calendar.October
        }

        Month {
            month: Calendar.February
        }

        Month {
            month: Calendar.May
        }

        Month {
            month: Calendar.August
        }

        Month {
            month: Calendar.November
        }


        Month {
            month: Calendar.March
        }

        Month {
            month: Calendar.June
        }

        Month {
            month: Calendar.September
        }

        Month {
            month: Calendar.December
        }

    }

    component Quarter : Text {
            id: qtext
            color: Colors.text
            Layout.alignment: Qt.AlignHCenter
    }

    component Month : Rectangle {
        id: monthComponent
        property alias month: grid.month
        property alias year: grid.year

        Layout.fillWidth: true
        Layout.fillHeight: true

        Layout.preferredHeight: calendarStart.height + 20
        Layout.preferredWidth: calendarStart.width + 20

        color: "white"

        ColumnLayout {
            id: calendarStart

            Text {
                Layout.alignment: Qt.AlignHCenter
                font.bold: true
                font.italic: true
                font.pixelSize: root.fontSize
                text: grid.locale.monthName(monthComponent.month)
                      + ' ' + grid.year
            }

            GridLayout {
                columns: 2

                DayOfWeekRow {
                    locale: grid.locale

                    Layout.column: 1
                    Layout.fillWidth: true
                }

                WeekNumberColumn {
                    month: grid.month
                    year: monthComponent.year
                    locale: grid.locale

                    Layout.fillHeight: true
                }

                MonthGrid {
                    id: grid
                    month: grid.month
                    year: 2023

                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    delegate: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: model.month === grid.month ? 1 : 0
                        text: model.day
                        font: grid.font
                        required property var model

                    }

                }
            }
        }
    }
}
