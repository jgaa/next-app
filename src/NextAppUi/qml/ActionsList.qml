import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB

Rectangle {
    id: root

    //property alias currentIndex : treeView.selectionModel.currentIndex
    property string selectedItemUuid: MainTreeModel.selected

    color: Colors.background

    ColumnLayout {
        anchors.fill: parent


        RowLayout {
            Layout.fillWidth: true
            height: 20

            Rectangle {
                Layout.fillWidth: true
                color: 'lightgray'
            }
        }

        ListView {
            id: listView
            property string currentNode: MainTreeModel.selected

            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true

            model: ActionsModel

            onCurrentNodeChanged: {
                console.log("Populating node ", currentNode)
                ActionsModel.populate(currentNode)
            }

            delegate: Text {
                text: name
            }
        }
    }
}
