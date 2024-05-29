import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import Nextapp.Models

Dialog {
    id: root
    property NextappPb.timeBlock tb
    property int controlsPreferredWidth: 200

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        console.log("Dialog opened :)");
        console.log("TimeBlock id is", tb.id_proto)
        assign()
    }

    function assign() {
        title.text = tb.name
        //category.currentIndex = tb.category
        start.text = NaCore.toDateAndTime(tb.timeSpan.start, 0)
        end.text = NaCore.toDateAndTime(tb.timeSpan.end, tb.timeSpan.start)
    }

    function save() {
        tb.name = title.text
        //tb.category = category.currentIndex
        tb.timeSpan.start = NaCore.parseDateOrTime(start.text, tb.timeSpan.start)
        tb.timeSpan.end = NaCore.parseDateOrTime(end.text, tb.timeSpan.start)

        // TODO: Validate that from and to dates are the same!
        // TODO: Category
        // TODO: Actions / type

        parent.model.updateTimeBlock(tb)
    }

    GridLayout {
        id: dlgfields
        Layout.alignment: Qt.AlignLeft
        Layout.fillHeight: true
        Layout.fillWidth: true
        rowSpacing: 4
        columns: 2

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Title")
        }

        DlgInputField {
            id: title
            Layout.preferredWidth: root.controlsPreferredWidth * 3
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Category")
        }

        StyledComboBox {
            id: category

            model: ListModel {
                ListElement { name: "Work" }
                ListElement { name: "Personal" }
                ListElement { name: "Family" }
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Start")
        }

        DlgInputField {
            id: start
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("End")
        }

        DlgInputField {
            id: end
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Actions")
        }

        ListView {

        }
    }

    onAccepted: {
        save()
        close()
        deleteLater()
    }

    onRejected: {
        close()
        deleteLater()
    }
}
