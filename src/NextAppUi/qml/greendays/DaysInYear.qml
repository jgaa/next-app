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
            delegate: GdQuarter {
                text: qsTr("Q") + modelData
            }
        }

        Repeater {
            model: [Calendar.January, Calendar.April, Calendar.July, Calendar.October,
                    Calendar.February, Calendar.May, Calendar.August, Calendar.November,
                    Calendar.March, Calendar.June, Calendar.September, Calendar.December]
            //model: [Calendar.January, Calendar.April, Calendar.July, Calendar.October]
            delegate: GdMonth {
                month: modelData
                year: 2024
                fontSize: root.fontSize
            }
        }
    }
}
