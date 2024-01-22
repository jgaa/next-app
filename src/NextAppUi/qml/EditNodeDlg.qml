import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

Dialog {
    id: root

    property alias name: name.text
    property string type: "folder"
    property bool isNew: true
    property var node: null
    property var parentIx: null
    property string parentUuid: ""
    property string currentUuid: ""

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Nodes and lists")

    onVisibleChanged: if(visible) name.focus = true

    RowLayout {
        anchors.fill: parent
        IconImage {
            source: "../icons/fontawsome/" + type + ".svg"
            Layout.fillHeight: false
            Layout.fillWidth: false
            Layout.preferredHeight: 120
            Layout.preferredWidth: 120
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
        // Add / update the node
        // if (node === null) {
        //     node = MainTreeModel.emptyNode();
        // }

        // console.log("name is ", name.text)

        // node.name = name.text;

        // console.log("node.name is ", node.name)

        // node.descr = "fuck!";

        // MainTreeModel.addNode(node, parentUuid, currentUuid);

        var args = {
            name: name.text,
            kind: type,
            parent: MainTreeModel.uuidFromModelIndex(parentIx)
        }

        MainTreeModel.addNode(args)

        close()
    }

    onRejected: {
        close()
    }
}

