import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import Nextapp.Models

Dialog {
    id: root
    title: tr("Work")
    property var ws: null
    property int controlsPreferredWidth: 200

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600
    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        console.log("Opened. ws.start=", ws.start)
        from.text = NaCore.toDateAndTime(ws.start)

        if (ws.hasEnd) {
            to.text = NaCore.toDateAndTime(ws.end)
        }
        status.currentIndex = ws.state
        status.enabled = ws.state !== 2
    }

    onAccepted: {
        console.log("Accepted")
    }
    onRejected: {
        console.log("Rejected")
    }

    onVisibleChanged: {
        console.log("VisibleChanged: ws: ", ws)
    }

    GridLayout {
        Layout.alignment: Qt.AlignLeft
        Layout.fillHeight: true
        Layout.fillWidth: true
        rowSpacing: 4
        columns: 2
        anchors.fill: parent
        anchors.margins: 10

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Name")
        }

        DlgInputField {
            id: name
            text: ws.name
            Layout.preferredWidth: root.controlsPreferredWidth * 3
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Status")
        }

        ComboBox {
            id: status

            Layout.preferredWidth: root.controlsPreferredWidth
            currentIndex: ws.state

            model: ListModel {
                ListElement{ text: qsTr("Active")}
                ListElement{ text: qsTr("Paused")}
                ListElement{ text: qsTr("Done")}
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("From")
        }

        DlgInputField {
            id: from
            Layout.preferredWidth: root.controlsPreferredWidth
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("To")
        }

        DlgInputField {
            id: to
            Layout.preferredWidth: root.controlsPreferredWidth
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Notes")
        }

        TextArea {
            id: notes
            //Layout.preferredHeight: 200
            Layout.fillHeight: true
            Layout.preferredWidth: root.controlsPreferredWidth * 3
            placeholderText: qsTr("Optional notes...")
            text: ws.notes
            background: Rectangle {
                color: notes.focus ? "lightblue" : "lightgray"
            }
        }
    }
}


