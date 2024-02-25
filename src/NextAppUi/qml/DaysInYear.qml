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

        Repeater {
            model: [1, 2, 3, 4]
            delegate: Quarter {
                text: qsTr("Q") + modelData
            }
        }

        Repeater {
            model: [Calendar.January, Calendar.April, Calendar.July, Calendar.October,
                    Calendar.February, Calendar.May, Calendar.August, Calendar.November,
                    Calendar.March, Calendar.June, Calendar.September, Calendar.December]
            delegate: Month {
                month: modelData
                year: 2024
            }
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
        property MonthModel mmodel: DaysModel.getMonth(year, month)
        property bool validColors: mmodel.validColors;

        //color: validColors ? "white" : "lightgray"
        //color: "lightgray"

        onValidColorsChanged: {
            console.log("Month ", grid.month, " validColors changed to ", monthComponent.validColors)
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
                        color: drect.model.month === monthComponent.month ? monthComponent.validColors ? monthComponent.mmodel.getColorForDayInMonth(drect.model.day) : "lightgray" : "white"
                        // TODO: Make a rounded background if the day has notes or report
                        //radius: 100

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
                                    //var uuid = monthComponent.mmodel.getUuidForDayInMonth(drect.model.day);

                                    var dmodel = DaysModel.getDay(drect.model.year, drect.model.month, drect.model.day);
                                    if (dmodel === null) {
                                        console.debug("Error: dmodel is null");
                                        return; // or maybe throw
                                    }

                                    var component = Qt.createComponent("../qml/DayDialog.qml");
                                    if( component.status === Component.Error ) {
                                        console.debug("Error:"+ component.errorString() );
                                        return; // or maybe throw
                                    }

                                    var dlg = component.createObject(monthComponent, {
                                         x: 25,
                                         y: 25,
                                         model: dmodel,
                                         date: new Date(drect.model.year, drect.model.month, drect.model.day)
                                         });

                                    if( component.status === Component.Error ) {
                                        console.debug("Error:"+ component.errorString() );
                                        return; // or maybe throw
                                    }

                                    dlg.open()
                                }
                            }
                        }
                    }// delegate
                }
            }
        }
    }
}
