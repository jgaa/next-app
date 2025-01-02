import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Rectangle {
    id: root
    property int month: 0
    property int year: 0
    property int fontSize: 12
    property var mmodel: NaGreenDaysModel.getMonth(year, month + 1)
    property bool validColors: mmodel.validColors

    color: MaterialDesignStyling.surface
    border.color: MaterialDesignStyling.outline
    border.width: 1

    onValidColorsChanged: {
        console.log("Month ", root.month, " validColors changed to ", root.validColors)
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
            text: grid.locale.monthName(root.month)
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

                delegate: Text {
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
                year: root.year
                month: root.month

                locale: grid.locale
                Layout.fillHeight: true

                delegate: Text {
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

                month: root.month
                year: root.year

                Layout.fillWidth: true
                Layout.fillHeight: true

                delegate: Rectangle {
                    id: drect
                    height: dtext.height
                    width: dtext.width
                    radius: 3
                    property string color_name: drect.model.month === root.month ? root.validColors ? root.mmodel.getColorForDayInMonth(drect.model.day) : "lightgray" : "transparent"
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
                        opacity: drect.model.month === root.month ? 1 : 0
                        text: drect.model.day
                        font: grid.font
                        color: drect.color_name === "transparent" ? MaterialDesignStyling.onSurface : "black"

                        MouseArea {
                            cursorShape: Qt.PointingHandCursor
                            anchors.fill: parent
                            visible: drect.model.month === root.month

                            hoverEnabled: true
                            onEntered: {
                                hover_shadow.opacity = 0.20
                            }
                            onExited: {
                                hover_shadow.opacity = 0
                            }

                            onDoubleClicked: {
                                //var uuid = root.mmodel.getUuidForDayInMonth(drect.model.day);

                                var dmodel = NaGreenDaysModel.getDay(drect.model.year, drect.model.month + 1, drect.model.day);
                                if (dmodel === null) {
                                    console.debug("Error: dmodel is null");
                                    return; // or maybe throw
                                }

                                var component = Qt.createComponent("DayDialog.qml");
                                if( component.status === Component.Error ) {
                                    console.debug("Error:"+ component.errorString() );
                                    return; // or maybe throw
                                }

                                var dlg = component.createObject(root, {
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
