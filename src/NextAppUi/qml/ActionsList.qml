import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
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

        Component {
            id: sectionHeading
            Rectangle {
                width: ListView.view.width
                height: childrenRect.height
                color: Colors.text

                required property string section

                Text {
                    id: label
                    text: ActionsModel.toName(parent.section)
                    font.bold: true
                    color: Colors.background
                }
            }
        }

        ListView {
            id: listView

            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true

            section {
                criteria: ViewSection.FullString
                property: "section"
                delegate: sectionHeading
            }

            model: ActionsModel

            delegate: Item {
                id: actionItem
                property bool selected: listView.currentIndex === index
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property bool favorite
                required property bool hasWorkSession
                required property string listName
                required property string node

                implicitHeight: row.implicitHeight + 4
                width: listView.width
                clip: true

                Rectangle {
                    id: background
                    color: selected ? Colors.selection : index % 2 ? Colors.background : Colors.background2
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

                DragHandler {
                    id: dragHandler
                    target: row
                }

                Drag.active: dragHandler.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.MoveAction
                Drag.mimeData: {
                    "text/app.nextapp.action": actionItem.uuid,
                    "text/app.nextapp.curr.node": actionItem.node
                }

                ColumnLayout {
                    id: row
                    Layout.fillWidth: true

                    RowLayout {
                        spacing: 6

                        Image {
                            id: checkIcon
                            Layout.topMargin: 2
                            Layout.bottomMargin: 2
                            source:  done ? "../icons/square-checked.svg" : "../icons/square-unchecked.svg"
                            sourceSize.height: font.pixelSize * 1.4
                            fillMode: Image.PreserveAspectFit

                            MouseArea {
                                anchors.fill: parent

                                onClicked: {
                                    ActionsModel.markActionAsDone(uuid, !done)
                                }
                            }
                        }

                        Text {
                            id: actionName
                            text: name
                            color: done ? Colors.disabledText : Colors.text
                        }

                        CheckBoxWithFontIcon {
                            id: favoriteIcon
                            isChecked: favorite
                            checkedCode: "\uf005"
                            uncheckedCode: "\uf005"
                            checkedColor: "orange"
                            uncheckedColor: "lightgray"
                            useSolidForChecked: true
                            iconSize: 16
                            autoToggle: false
                            text: ""

                            onClicked: {
                                ActionsModel.markActionAsFavorite(uuid, !favorite)
                            }
                        }

                        CheckBoxWithFontIcon {
                            id: canWorkIcon
                            isChecked: hasWorkSession
                            checkedCode: "\uf017"
                            uncheckedCode: "\uf017"
                            checkedColor: "green"
                            uncheckedColor: "lightblue"
                            useSolidForChecked: true
                            iconSize: 16
                            autoToggle: false

                            onClicked: {
                                WorkSessionsModel.startWork(uuid)
                            }
                        }
                    }

                    RowLayout {
                        Item {
                            Layout.preferredWidth: 20
                        }

                        Label {
                            color: done ? Colors.disabledText : Colors.nodePath
                            text: listName
                            visible: text !== ""
                        }
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
            text: qsTr("Start Work Session")
            icon.source: "../icons/fontawsome/clock.svg"
            onTriggered: {
                WorkSessionsModel.startWork(contextMenu.uuid)
            }
        }
        Action {
            text: qsTr("Add completed Work Session")
            icon.source: "../icons/fontawsome/clock.svg"
            onTriggered: {
                openAddWorkDialog(contextMenu.uuid, contextMenu.name)
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

    function openAddWorkDialog(uuid, name) {
        openDialog("EditWorkSession.qml", {
            ws: WorkSessionsModel.createSession(uuid, name),
            title: qsTr("Add Work Session"),
            model: WorkSessionsModel
        });
    }
}
