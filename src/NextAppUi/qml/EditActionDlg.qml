import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb

Dialog {
    id: root
    property string node: MainTreeModel.selected
    property ActionPrx aprx
    property NextappPb.action action: aprx.action
    property bool assigned: false
    property bool valid: aprx.valid
    property int controlsPreferredWidth: 200

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: root.aprx.valid ? (Dialog.Ok | Dialog.Cancel) : Dialog.Cancel

    onOpened: {
        console.log("Dialog opened :)");
        console.log("action.name is", action.name)
        assign()
    }

    onValidChanged: {
        if (valid) {
            assign()
        } else {
            // TODO: Popup
            console.log("Failed to fetch existing Action")
        }
    }

    function assign() {
        if (aprx.valid && !root.assigned) {

            //root.action = aprx.action

            // Set the values in the controld. We can't bind them directly for some reason.
            status.currentIndex = root.action.status
            name.text = root.action.name = action.name
            descr.text = root.action.descr
            priority.currentIndex = root.action.priority

            if (action.node === "") {
                action.node = node;
            }

            if (action.node === "") {
                throw "No node"
            }

            // Don't do it again for this instance
            root.assigned = true
        }
    }

    RowLayout {
        visible: root.aprx.valid
        anchors.fill: parent
        // Image {
        //     width: 96
        //     height: 96
        //     source: root.icon
        //     sourceSize.width: 96
        //     sourceSize.height: 96
        //     fillMode: Image.PreserveAspectFit
        // }

        GridLayout {
            Layout.alignment: Qt.AlignLeft
            Layout.fillHeight: true
            Layout.fillWidth: true
            rowSpacing: 4
            columns: 2

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Name")
            }

            DlgInputField {
                id: name
                //text: root.action.name
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Status")
            }

            ComboBox {
                id: status
                Layout.preferredWidth: root.controlsPreferredWidth
                model: ListModel {
                    ListElement{ text: qsTr("Active")}
                    ListElement{ text: qsTr("Done")}
                    ListElement{ text: qsTr("On Hold")}
                }
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("When")
            }

            WhenControl {
                //width: root.controlsPreferredWidth * 2
                id: whenControl
                when: root.action.dueByTime
                dueType: root.action.dueType
                Layout.preferredWidth: root.controlsPreferredWidth

                // onDueTypeChanged: (when, dueType) =>  {
                //     console.log("DueType changed to", dueType)
                //     root.action.dueByTime = when
                //     root.action.dueType = dueType
                // }

                onSelectionChanged: {
                    console.log("DueType changed to", whenControl.dueType)
                    root.action.dueByTime = whenControl.when
                    root.action.dueType = whenControl.dueType
                }
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Description")
            }

            TextArea {
                id: descr
                Layout.preferredHeight: 200
                Layout.preferredWidth: 600
                placeholderText: qsTr("Some words to describe the purpose of this item?")
                //text: root.action.descr

                background: Rectangle {
                    color: descr.focus ? "lightblue" : "lightgray"
                }
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Priority")
            }

            ComboBox {
                id: priority
                //currentIndex: kinds.indexOf(root.action.priority, 0)
                model: ListModel {
                    ListElement{ text: qsTr("Critical")}
                    ListElement{ text: qsTr("Very Important")}
                    ListElement{ text: qsTr("Higher")}
                    ListElement{ text: qsTr("High")}
                    ListElement{ text: qsTr("Normal")}
                    ListElement{ text: qsTr("Medium")}
                    ListElement{ text: qsTr("Low")}
                    ListElement{ text: qsTr("Insignificant")}
                }
            }
        }
    }

    onAccepted: {
        root.action.status = status.currentIndex
        root.action.name = name.text;
        root.action.descr = descr.text
        root.action.priority = priority.currentIndex

        if (root.action.id_proto !== "") { // edit
            ActionsModel.updateAction(root.action)
        } else {
            ActionsModel.addAction(root.action)
        }

        close()
    }

    onRejected: {
        close()
    }
}

