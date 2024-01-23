import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

Dialog {
    id: root

    property alias name: name.text
    property string kind: "folder"
    property bool isNew: true
    property var parentIx: null
    property var node: null
    property string parentUuid: ""
    property string currentUuid: ""

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
        console.log("Dialog opened :)");

        if (node !== null) {
            root.name = node.name
            root.kind = node.kind
            root.isNew = false
            root.parentUuid = node.parent
        }
    }

    RowLayout {
        anchors.fill: parent
        Image {
            source: "../icons/" + kind + ".svg"
            // Hack to scale vector-images in Qml (which should *not* require a hack...)
            // https://forum.qt.io/topic/52161/properly-scaling-svg-images
            sourceSize: Qt.size(120, 120)
            Image {
                id: img
                source: parent.source
                width: 0
                height: 0
            }
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

            DlgInputField {
                id: name
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("what")
            }

            DlgInputField {
                id: what
            }
        }
    }

    onAccepted: {
        var args = {
            name: name.text,
            kind: kind,
            parent: node ? node.parent : MainTreeModel.uuidFromModelIndex(parentIx)
        }

        if (node != null) { // edit
            //let x = {...node, ...args}; Don't work
            let new_args = {}
            Object.assign(new_args, node, args)
            MainTreeModel.updateNode(new_args)
            //console.log("new_args.name=", new_args.name, " new_args.uuid=", new_args.uuid)
        } else {
            MainTreeModel.addNode(args)
        }

        close()
    }

    onRejected: {
        close()
    }
}

