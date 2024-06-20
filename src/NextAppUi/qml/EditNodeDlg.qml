import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Dialog {
    id: root

    property alias name: name.text
    property alias active: active.checked
    property alias descr: descr.text
    property string kind: "folder"
    property bool isNew: true
    property var node: null
    property string parentUuid: NaMainTreeModel.selected
    property string currentUuid: ""
    property var kinds: ["folder", "organization", "person", "project", "task"]
    property string icon: "../icons/folder/.svg"

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Nodes and lists")

    onVisibleChanged: {
        if(visible) {
            name.focus = true

            //if (current)
        }
    }

    onOpened: {
        // console.log("Dialog opened :)");

        if (node !== null) {

            // console.log("EditNode - node: ", JSON.stringify(node))

            root.active = node.active
            root.name = node.name
            root.kind = node.kind
            root.isNew = false
            root.parentUuid = node.parent
            root.descr = node.descr
        }
    }

    RowLayout {
        anchors.fill: parent
        Image {
            width: 96
            height: 96
            source: root.icon
            sourceSize.width: 96
            sourceSize.height: 96
            fillMode: Image.PreserveAspectFit
        }

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

            TextField{
                id: name
            }

            Item {}

            CheckBox {
                id: active
                checked: true
                text: qsTr("Active")
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

                background: Rectangle {
                    color: descr.focus ? "lightblue" : "lightgray"
                }
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Kind")
            }

            ComboBox {
                id: kind
                currentIndex: kinds.indexOf(root.kind, 0)
                model: ListModel {
                    ListElement{ text: qsTr("folder")}
                    ListElement{ text: qsTr("organization")}
                    ListElement{ text: qsTr("person")}
                    ListElement{ text: qsTr("project")}
                    ListElement{ text: qsTr("task")}
                }

                onCurrentIndexChanged: {
                    if (currentIndex >= 0 || currentIndex >= kinds.length) {
                        root.kind = kinds[currentIndex]
                    } else {
                        root.kind = kinds[0]
                    }

                    // console.log("root.kind is ", root.kind)
                    root.icon = "../icons/" + root.kind + ".svg"
                }
            }
        }
    }

    onAccepted: {
        var args = {
            name: root.name,
            kind: root.kind,
            active: root.active,
            parent: root.parentUuid,
            descr: root.descr
        }

        if (node != null) { // edit
            //let x = {...node, ...args}; Don't work
            let new_args = {}
            Object.assign(new_args, node, args)
            NaMainTreeModel.updateNode(new_args)
        } else {
            NaMainTreeModel.addNode(args)
        }

        close()
    }

    onRejected: {
        close()
    }
}

