import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
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

    Layout.preferredHeight: calendarStart.implicitHeight + 20
    Layout.preferredWidth: calendarStart.implicitWidth + 20

    ColumnLayout {
        id: calendarStart

        Text {
            Layout.alignment: Qt.AlignHCenter
            font.bold: true
            font.italic: true
            font.pixelSize: root.fontSize
            text: grid.locale.monthName(root.month + 1)
                  + ' ' + grid.year
            color: MaterialDesignStyling.onSurfaceVariant
        }

        DateCalendar {
            id: grid
            year: root.year
            month: root.month
            firstDayOfWeek: NaComm.globalSettings.firstDayOfWeekIsMonday ? Qt.Monday : Qt.Sunday
            cellWidth: Math.max(18, root.fontSize + 8)
            cellHeight: Math.max(18, root.fontSize + 8)
            weekNumberWidth: Math.max(20, root.fontSize + 10)
            showToday: true
            todayBackground: "lightgreen"
            weekNumberColor: MaterialDesignStyling.onSurfaceVariant
            dayBackgroundProvider: function(date) {
                return root.validColors
                    ? root.mmodel.getColorForDayInMonth(date.getDate()) : "lightgray"
            }

            onDateActivated: function(date) {
                var dmodel = NaGreenDaysModel.getDay(date.getFullYear(), date.getMonth() + 1, date.getDate())
                if (dmodel === null) {
                    console.debug("Error: dmodel is null")
                    return
                }

                var component = Qt.createComponent("DayDialog.qml")
                if (component.status === Component.Error) {
                    console.debug("Error:" + component.errorString())
                    return
                }

                var dlg = component.createObject(root, {
                    x: 25,
                    y: 25,
                    model: dmodel,
                    date: date
                })
                if (component.status === Component.Error) {
                    console.debug("Error:" + component.errorString())
                    return
                }
                dlg.open()
            }
        }
    }
}
