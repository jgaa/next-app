import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Dialog {
    id: dayDlg
    property var date
    property var model
    property alias notes: notes.text
    property alias report: report.text
    property bool valid: model.valid
    property bool colorts_initialized: false

    title: {
        return qsTr("Day %1").arg(date.toLocaleDateString())
    }

    standardButtons: Dialog.Cancel

    function enableSave() {
         standardButtons = Dialog.Save | Dialog.Cancel
    }

    onValidChanged: {
        // console.log("DayDialog: Valid changed to ", valid)
        // console.log("Report is ", model.report)

        // if (valid && !colorts_initialized) {
        //     //select.currentIndex = NaDayColorModel.getIndexForColorUuid(model.colorUuid)
        // }
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
            model: NaDayColorModel.names
            currentIndex: dayDlg.model.valid ? NaDayColorModel.getIndexForColorUuid(dayDlg.model.colorUuid) : -1

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
            text: dayDlg.model.valid ? model.notes : ""
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
            text: dayDlg.model.valid ? model.report : ""
            Layout.preferredHeight: 100
            Layout.preferredWidth: 500
            placeholderText: qsTr("Your Daily Report")

            background: Rectangle {
                color: report.focus ? "lightblue" : "lightgray"
            }
        }
    }

    onAccepted: {
        model.colorUuid = NaDayColorModel.getUuid(select.currentIndex)
        model.notes = notes.text
        model.report = report.text
        model.commit();
    }
}
