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
    property var model: null

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600
    standardButtons: Dialog.Cancel

    onOpened: {
        // console.log("Opened. ws.start=", ws.start)
        from.text = NaCore.toDateAndTime(ws.start, 0)
        notes.text = ws.notes
        paused.text = NaCore.toHourMin(ws.paused)

        if (ws.hasEnd) {
            to.text = NaCore.toDateAndTime(ws.end, ws.start)
        }
        status.currentIndex = ws.state
        status.enabled = ws.state !== 2

        somethingChanged()
    }

    onAccepted: {
        // console.log("Accepted")

        ws.start = NaCore.parseDateOrTime(from.text, 0)
        if (to.text.trim() !== "") {
            ws.end = NaCore.parseDateOrTime(to.text, ws.start)
        }
        ws.paused = NaCore.parseHourMin(paused.text)
        ws.state = status.currentIndex
        ws.name = name.text
        ws.notes = notes.text

        model.update(ws)
    }

    onRejected: {
        // console.log("Rejected")
    }

    onVisibleChanged: {
        // console.log("VisibleChanged: ws: ", ws)
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

            onChanged: {
                somethingChanged()
            }
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

            onCurrentIndexChanged: {
                somethingChanged()
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

            onChanged: {
                // console.log("From changed")
                somethingChanged()
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("To")
        }

        DlgInputField {
            id: to
            Layout.preferredWidth: root.controlsPreferredWidth

            onChanged: {
                somethingChanged()
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            color: Colors.disabledText
            text: qsTr("Paused")
        }

        DlgInputField {
            id: paused
            Layout.preferredWidth: root.controlsPreferredWidth

            onChanged: {
                somethingChanged()
            }
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

            onTextChanged: {
                somethingChanged()
            }
        }

        Label {
            id: errors
            Layout.alignment: Qt.AlignLeft
            color: "red"
        }
    }

    function somethingChanged() {
        root.standardButtons = validate() ? Dialog.Save | Dialog.Cancel :  Dialog.Cancel
    }

    function validate() {
        errors.text = ""

        if (name.text === "") {
            errors.text = qsTr("Name is required")
            return false
        }

        if (from.text === "") {
            errors.text = qsTr("From is required")
            return false
        }

        var ts_from = NaCore.parseDateOrTime(from.text);
        if (ts_from === -1) {
            errors.text = qsTr("Invalid date/time in from field")
            return false
        }

        if (to.text.trim() !== "") {
            var ts_to = NaCore.parseDateOrTime(from.text);
            if (ts_to === -1) {
                errors.text = qsTr("Invalid date/time in to field")
                return false
            }

            if (ts_to < ts_from) {
                errors.text = qsTr("To date/time must be after from date/time")
                return false
            }

            if (ws.state !== 2) {
                errors.text = qsTr("Work session with end-time must be in done state")
                return false
            }
        }

        if (ws.id_proto === "") {
            // New work.
            if (to.text.trim() === "") {
                errors.text = qsTr("End time is required for new work")
                return false
            }
        }

        return true
    }
}


