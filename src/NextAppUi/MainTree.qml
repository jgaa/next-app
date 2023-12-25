import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: mainTree

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            Layout.fillWidth: true
            height: 20

            Rectangle {
                anchors.fill: parent
                color: 'lightgray'
            }
        }

        TreeView {
            anchors.left: parent.left
            anchors.right: parent.right
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: treeModel
            delegate: TreeViewDelegate {}
        }
    }
}
