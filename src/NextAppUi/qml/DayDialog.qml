import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

Dialog {
    id: dayDlg
    property Date date
    property DayModel model
    property alias notes: notes.text
    property alias report: report.text

    title: {
        return qsTr("Day %1").arg(date.toLocaleDateString())
    }

    standardButtons: Dialog.Cancel

    function enableSave() {
         standardButtons = Dialog.Save | Dialog.Cancel
    }

    GridLayout {
        Layout.alignment: Qt.AlignLeft
        Layout.fillHeight: true
        Layout.fillWidth: true
        rowSpacing: 4
        columns: 2
        enabled: model.valid

        Label { text: qsTr("Day Color") }
        ComboBox {
            id: select
            model: DayColorModel.names
            currentIndex: { return DayColorModel.getIndexForColorUuid(model.colorUuid)}

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

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Notes")
        }

        TextArea {
            id: notes
            text: model.notes
            Layout.preferredHeight: 100
            Layout.preferredWidth: 500
            placeholderText: qsTr("Some words to describe your day?")

            background: Rectangle {
                color: notes.focus ? "lightblue" : "lightgray"
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Report")
        }

        TextArea {
            id: report
            text: model.report
            Layout.preferredHeight: 100
            Layout.preferredWidth: 500
            placeholderText: qsTr("Your Daily Report")

            background: Rectangle {
                color: report.focus ? "lightblue" : "lightgray"
            }
        }
    }

    onAccepted: {
        model.colorUuid = DayColorModel.getUuid(select.currentIndex)
        model.commit();
    }
}
