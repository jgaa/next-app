import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

Dialog {
    id: dayDlg
    property int year;
    property int month;
    property int day;
    property string colorUuid;
    title: {
        let date = new Date(year, month, day)
        return qsTr("Day %1").arg(date.toLocaleDateString())
    }

    standardButtons: Dialog.Cancel

    function enableSave() {
         standardButtons = Dialog.Save | Dialog.Cancel
    }

    ColumnLayout {
        RowLayout {

            Label { text: qsTr("Day Color") }
            ComboBox {
                id: select
                model: DayColorModel.names
                currentIndex: { return DayColorModel.getIndexForColorUuid(dayDlg.colorUuid)}

                delegate: ItemDelegate {
                    width: parent.width
                    required property int index
                    required property string modelData
                    text: modelData;
                }

                onCurrentIndexChanged: {
                    if (currentIndex >= 0) {
                        dayDlg.enableSave()
                    }
                }

            }
        }
    }

    onAccepted: {
        DayColorModel.setDayColor(year, month, day, select.currentIndex)
    }
}
