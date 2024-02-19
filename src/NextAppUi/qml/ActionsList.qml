import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB

Rectangle {
    id: root
    anchors.fill: parent

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

            // delegate: ItemDelegate {
            //     //text: name
            //     highlighted: ListView.isCurrentItem
            //     required property int index
            //     required property string name
            //     onClicked: listView.currentIndex = index
            // }


            delegate: Item {
                property bool selected: listView.currentIndex === index
                required property int index
                required property string name
                required property string uuid
                required property bool done

                implicitHeight: row.implicitHeight
                width: listView.width
                clip: true

                Rectangle {
                    id: background
                    color: selected ? Colors.selection : Colors.background
                    anchors.fill: parent
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onSingleTapped: (eventPoint, button) => {
                        switch (button) {
                            case Qt.LeftButton:
                                listView.currentIndex = index
                                console.log("Actions: Current selection is ", index)
                                break;
                            case Qt.RightButton:
                                contextMenu.uuid = uuid
                                contextMenu.popup();
                        }
                    }
                }

                RowLayout {
                    id: row
                    spacing: 6
                    //height: doneCtl.height
                    // Priority color

                    StyledCheckBox {
                        id: doneCtl
                        height: name.height
                        checked: done
                    }

                    Text {
                        text: name
                        color: Colors.text
                    }
                }
            }
        }
    }

    MyMenu {
        id: contextMenu
        property string uuid
        Action {
            text: qsTr("Edit")
            icon.source: "../icons/fontawsome/pen-to-square.svg"
            onTriggered: {
                openActionDlg(contextMenu.uuid)
            }
        }
        // Action {
        //     icon.source: "../icons/fontawsome/trash-can.svg"
        //     text: qsTr("Delete")
        //     onTriggered: {
        //         confirmDelete.node = contextMenu.node
        //         confirmDelete.open()
        //     }
        // }
    }

    function openActionDlg(uuid) {
        openDialog("EditActionDlg.qml", {
            node: mainTree.selectedItemUuid,
            title: qsTr("Edit Action"),
            aprx: ActionsModel.getAction(uuid)
        });
    }
}
