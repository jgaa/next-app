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
        property MonthModel mmodel: ServerComm.getMonthModel(year, month)
        property bool validColors: mmodel.validColors;

        //color: validColors ? "white" : "lightgray"
        //color: "lightgray"

        onValidColorsChanged: {
            let x = 1
        }

        Layout.fillWidth: true
        Layout.fillHeight: true

        Layout.preferredHeight: calendarStart.height + 20
        Layout.preferredWidth: calendarStart.width + 20

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
                    year: 2024

                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    delegate: Rectangle {
                        id: drect
                        height: dtext.height
                        width: dtext.width
                        required property var model
                        color: {
                            // if (hover.hovered) {
                            //     return "lightpink"
                            // }

                            if (!monthComponent.validColors) {
                                return "lightgray";
                            }

                            if (drect.model.month !== grid.month) {
                                return "white"
                            }

                            var mday = drect.model.day
                            const c = monthComponent.mmodel.getColorForDayInMonth(mday);
                            if (typeof c !== 'string' || c === "") {
                                return "gray";
                            }
                            return c;
                        }

                        Rectangle {
                            id: hover_shadow
                            anchors.fill: parent
                            opacity: 0
                            color: "black"
                        }

                        //color: "white"
                        Text {
                            id: dtext
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            opacity: drect.model.month === grid.month ? 1 : 0
                            text: drect.model.day
                            font: grid.font

                            MouseArea {
                                cursorShape: Qt.PointingHandCursor
                                anchors.fill: parent
                                visible: drect.model.month === grid.month

                                hoverEnabled: true
                                onEntered: {
                                    hover_shadow.opacity = 0.20
                                }
                                onExited: {
                                    hover_shadow.opacity = 0
                                }

                                onDoubleClicked: {
                                    var uuid = monthComponent.mmodel.getUuidForDayInMonth(drect.model.day);
                                    var component = Qt.createComponent("../qml/DayDialog.qml");
                                    var dlg = component.createObject(monthComponent, {
                                         x: 25,
                                         y: 25,
                                         year: drect.model.year,
                                         month: drect.model.month,
                                         day: drect.model.day,
                                         colorUuid: uuid
                                         });
                                    dlg.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
