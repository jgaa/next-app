import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Rectangle {
    id: root
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5
    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight

    property int fontSize: 16

    color: MaterialDesignStyling.surface

    GridLayout {
        id: grid
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

    component Quarter : Item {
        property alias text: qtext.text

        Layout.topMargin: 10
        Layout.fillWidth: true
        Layout.preferredHeight: 40

        Rectangle {
            anchors.fill: parent
            color: MaterialDesignStyling.secondaryContainer
            radius: 5
        }

        Text {
            id: qtext
            anchors.centerIn: parent
            color: MaterialDesignStyling.onSecondaryContainer
        }
    }

    component Month : Rectangle {
        id: monthComponent
        radius: 5
        property alias month: grid.month
        property alias year: grid.year
        property MonthModel mmodel: DaysModel.getMonth(year, month)
        property bool validColors: mmodel.validColors;

        color: MaterialDesignStyling.surface
        border.color: MaterialDesignStyling.outline
        border.width: 1

        onValidColorsChanged: {
            // console.log("Month ", grid.month, " validColors changed to ", monthComponent.validColors)
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
                color: MaterialDesignStyling.onSurfaceVariant
            }

            GridLayout {
                columns: 2

                DayOfWeekRow {
                    id: dayOfWeekRow
                    locale: grid.locale

                    Layout.column: 1
                    Layout.fillWidth: true

                    delegate: Label {
                        required property string shortName
                        text: shortName
                        font: dayOfWeekRow.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: MaterialDesignStyling.onSurfaceVariant
                    }
                }

                WeekNumberColumn {
                    id: weekNumberCol
                    month: grid.month
                    year: monthComponent.year
                    locale: grid.locale
                    Layout.fillHeight: true

                    delegate: Label {
                        required property int weekNumber
                        text: weekNumber
                        font: weekNumberCol.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: MaterialDesignStyling.onSurfaceVariant
                    }
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
                        radius: 3
                        property string color_name: drect.model.month === monthComponent.month ? monthComponent.validColors ? monthComponent.mmodel.getColorForDayInMonth(drect.model.day) : "lightgray" : "transparent"
                        required property var model
                        color: color_name
                        //color: MaterialDesignStyling.primary
                        // TODO: Make a rounded background if the day has notes or report
                        //radius: 100

                        Rectangle {
                            id: hover_shadow
                            anchors.fill: parent
                            opacity: 0
                            radius: 3
                            color: MaterialDesignStyling.scrim
                        }

                        Text {
                            id: dtext
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            opacity: drect.model.month === grid.month ? 1 : 0
                            text: drect.model.day
                            font: grid.font
                            color: drect.color_name === "transparent" ? MaterialDesignStyling.onSurface : "black"

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
