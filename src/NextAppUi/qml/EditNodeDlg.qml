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

    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(700, NaCore.height, Screen.height)

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Nodes and lists")

    onVisibleChanged: {
        if(visible) {
            name.focus = true
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

    GridLayout {
        anchors.fill: parent
        columns: NaCore.isMobile ? 1 : 2

        Image {
            width: NaCore.isMobile ? 36 : 96
            height: NaCore.isMobile ? 36 : 96
            source: root.icon
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
        }

        GridLayout {
            Layout.alignment: Qt.AlignLeft
            Layout.fillHeight: true
            Layout.fillWidth: true
            rowSpacing: 4
            columns: NaCore.isMobile ? 1 : 2

            Label {
                Layout.alignment: Qt.AlignLeft
                text: qsTr("Name")
            }

            TextField{
                Layout.fillWidth: true
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
                text: qsTr("Description")
            }

            TextArea {
                id: descr
                Layout.fillHeight: true
                Layout.fillWidth: true
                //placeholderText: qsTr("Some words to describe the purpose of this item?")

                background: Rectangle {
                    color: descr.focus ? "lightblue" : "lightgray"
                }
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                text: qsTr("Kind")
            }

            ComboBox {
                id: kind
                currentIndex: kinds.indexOf(root.kind, 0)
                Layout.fillWidth: true
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

