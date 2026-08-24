import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

Item {
    id: root

    property int year: new Date().getFullYear()
    // Matches QML Calendar and JavaScript Date: January is zero.
    property int month: new Date().getMonth()
    property int firstDayOfWeek: Qt.Monday
    // Use NextApp's translation language, never the operating-system locale.
    property var locale: Qt.locale(NaComm.globalSettings.language || "en")
    property date selectedDate: new Date()
    property bool selectionIsWeek: false
    property date highlightedDate: new Date(NaN)
    property bool showToday: true
    property color todayBackground: "lightgreen"
    property color selectedBackground: "yellow"
    property color selectedWeekBackground: "#e8e3ef"
    property color highlightedBackground: "#b9d7ff"
    property color weekModeDayTextColor: "#4a4458"
    // Leave dayTextColor empty to choose a foreground with the best contrast.
    // Set it only when a caller must supply a specific foreground color.
    property string dayTextColor: ""
    property color normalDayTextColor: MaterialDesignStyling.onSurface
    property color weekdayTextColor: normalDayTextColor
    property color outOfMonthTextColor: "transparent"
    property color weekNumberColor: "#555555"
    property int cellWidth: 28
    property int cellHeight: 24
    property int weekNumberWidth: 28
    // Return a color for a date; transparent leaves the normal background.
    property var dayBackgroundProvider: function(date) { return "transparent" }
    property bool interactive: true
    property bool weekNumbersInteractive: false

    signal dateActivated(var date)
    signal weekActivated(var weekStart, int weekNumber)

    implicitWidth: weekNumberWidth + 7 * cellWidth
    implicitHeight: weekdayHeader.height + calendarGrid.height

    function isSameDate(a, year, month, day) {
        return a instanceof Date && !isNaN(a.getTime())
            && a.getFullYear() === year && a.getMonth() + 1 === month && a.getDate() === day
    }

    function weekStartForDate(date) {
        var weekStart = new Date(date.getFullYear(), date.getMonth(), date.getDate())
        weekStart.setDate(weekStart.getDate() - (weekStart.getDay() + 6) % 7)
        return weekStart
    }

    function isSameWeek(first, second) {
        var firstWeekStart = weekStartForDate(first)
        var secondWeekStart = weekStartForDate(second)
        return isSameDate(firstWeekStart, secondWeekStart.getFullYear(), secondWeekStart.getMonth() + 1, secondWeekStart.getDate())
    }

    function linearChannel(value) {
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
    }

    function readableTextColor(background) {
        var luminance = 0.2126 * linearChannel(background.r)
                + 0.7152 * linearChannel(background.g)
                + 0.0722 * linearChannel(background.b)
        // Select the foreground with the larger WCAG contrast ratio.
        return (luminance + 0.05) / 0.05 >= 1.05 / (luminance + 0.05) ? "black" : "white"
    }

    DateCalendarModel {
        id: calendar
        year: root.year
        month: root.month + 1
        firstDayOfWeek: root.firstDayOfWeek
        locale: root.locale
    }

    GridLayout {
        id: weekdayHeader
        columns: 8
        columnSpacing: 0
        rowSpacing: 0
        anchors.top: parent.top

        Item { Layout.preferredWidth: root.weekNumberWidth; Layout.preferredHeight: root.cellHeight }

        Repeater {
            model: calendar.weekdayNames
            delegate: Label {
                required property string modelData
                text: modelData
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: root.weekdayTextColor
                Layout.preferredWidth: root.cellWidth
                Layout.preferredHeight: root.cellHeight
                elide: Text.ElideRight
            }
        }
    }

    GridLayout {
        id: calendarGrid
        anchors.top: weekdayHeader.bottom
        columns: 8
        columnSpacing: 0
        rowSpacing: 0

        Repeater {
            model: calendar
            delegate: Item {
                required property bool isWeekNumber
                required property int weekNumber
                required property var weekDate
                required property int year
                required property int month
                required property int day
                required property bool inCurrentMonth
                required property bool isToday

                property color customBackground: !isWeekNumber && inCurrentMonth
                    ? root.dayBackgroundProvider(new Date(year, month - 1, day)) : "transparent"
                property var cellDate: isWeekNumber ? weekDate : new Date(year, month - 1, day)
                property bool isSelectedWeek: root.selectionIsWeek
                    && root.isSameWeek(root.selectedDate, cellDate)
                property color backgroundColor: {
                    if (isWeekNumber) return isSelectedWeek ? root.selectedWeekBackground : "transparent"
                    if (!inCurrentMonth) return "transparent"
                    // Today stays visible even when its week is selected.
                    if (root.selectionIsWeek && root.showToday && isToday) return root.todayBackground
                    if (isSelectedWeek) return root.selectedWeekBackground
                    if (root.isSameDate(root.selectedDate, year, month, day)) return root.selectedBackground
                    if (root.isSameDate(root.highlightedDate, year, month, day)) return root.highlightedBackground
                    if (customBackground.a > 0) return customBackground
                    if (root.showToday && isToday) return root.todayBackground
                    return "transparent"
                }
                property color foregroundColor: {
                    if (isWeekNumber) return root.weekNumberColor
                    if (!inCurrentMonth) return root.outOfMonthTextColor
                    if (root.dayTextColor !== "") return root.dayTextColor
                    if (root.selectionIsWeek) return root.weekModeDayTextColor
                    if (backgroundColor.a > 0) return root.readableTextColor(backgroundColor)
                    return root.normalDayTextColor
                }

                Layout.preferredWidth: isWeekNumber ? root.weekNumberWidth : root.cellWidth
                Layout.preferredHeight: root.cellHeight

                Rectangle {
                    anchors.fill: parent
                    visible: !parent.isWeekNumber
                    color: parent.backgroundColor
                }

                Label {
                    anchors.fill: parent
                    text: parent.isWeekNumber ? parent.weekNumber : parent.day
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: (parent.isWeekNumber && root.selectionIsWeek && parent.isSelectedWeek)
                               || (!parent.isWeekNumber && root.showToday && parent.isToday)
                    color: parent.foregroundColor
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: parent.isWeekNumber ? root.weekNumbersInteractive
                                                 : root.interactive && parent.inCurrentMonth
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (parent.isWeekNumber)
                            root.weekActivated(parent.weekDate, parent.weekNumber)
                        else
                            root.dateActivated(new Date(parent.year, parent.month - 1, parent.day))
                    }
                }
            }
        }
    }
}
