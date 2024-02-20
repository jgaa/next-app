import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
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
                                contextMenu.name = name
                                contextMenu.popup();
                        }
                    }
                }

                RowLayout {
                    id: row
                    spacing: 6

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
        property string name

        Action {
            text: qsTr("Edit")
            icon.source: "../icons/fontawsome/pen-to-square.svg"
            onTriggered: {
                openActionDlg(contextMenu.uuid)
            }
        }
        Action {
            icon.source: "../icons/fontawsome/trash-can.svg"
            text: qsTr("Delete")
            onTriggered: {
                confirmDelete.uuid = contextMenu.uuid
                confirmDelete.name = contextMenu.name
                confirmDelete.open()
            }
        }
    }

    MessageDialog {
        id: confirmDelete

        property string uuid;
        property string name;

        title: qsTr("Do you really want to delete the Action \"%1\" ?").arg(name)
        text: qsTr("Note that any sub-items and all related information, including worked time, etc. will also be deleted! This action can not be undone.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           ActionsModel.deleteAction(uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
        }
    }

    function openActionDlg(uuid) {
        openDialog("EditActionDlg.qml", {
            node: mainTree.selectedItemUuid,
            title: qsTr("Edit Action"),
            aprx: ActionsModel.getAction(uuid)
        });
    }
}
