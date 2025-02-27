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
    property int year: new Date().getFullYear()

    color: MaterialDesignStyling.surface

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true

        contentWidth: grid.implicitWidth
        contentHeight: grid.implicitHeight

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
                    year: root.year
                    fontSize: root.fontSize
                }
            }
        }

        RowLayout {
            x: grid.x + 10
            y: grid.y + 15
            height: 20
            width: grid.width - 20

            CheckBoxWithFontIcon {
                uncheckedCode: "\uf0d9"
                autoToggle: false
                useSolidForAll: true

                onClicked: {
                    root.year -= 1
                }
            }

            Item {
                Layout.fillWidth: true
            }

            CheckBoxWithFontIcon {
                uncheckedCode: "\uf0da"
                autoToggle: false
                useSolidForAll: true

                onClicked: {
                    root.year += 1
                }
            }
        }
    }
}
