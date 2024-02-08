import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb

Dialog {
    id: root
    property string node: MainTreeModel.selected
    //property NextappPb.action action
    property NextappPb.action action: ActionsModel.newAction()

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        console.log("Dialog opened :)");

        console.log("action.name is", action.name)

        if (action.id_proto === "") {
            action.proprity = NextappPb.ActionPriority.PRI_NORMAL;
        }

        // Set the values in the controld. We can't bind them directly for some reason.
        name.text = root.action.name = action.name
        done.checked = root.action.completed
        descr.text = root.action.descr

        if (action.node === "") {
            action.node = node;
        }

        if (action.node === "") {
            throw "No node"
        }
    }

    RowLayout {
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

            Item {}

            CheckBox {
                id: done
                //checked: root.action.completed
                text: qsTr("Done")
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Description")
            }

            TextArea {
                id: descr
                Layout.preferredHeight: 250
                Layout.preferredWidth: 400
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
        // var args = {
        //     name: root.name,
        //     kind: root.kind,
        //     active: root.active,
        //     parent: root.parentUuid,
        //     descr: root.descr
        // }

        root.action.name = name.text;
        root.action.completed = done.checked
        root.action.descr = descr.text
       // action.priority =

        console.log("action.name", action.name, " action.descr=", action.descr, " descr.text", descr.text, " action.completed=", action.completed, " done.checked=", done.checked)

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

