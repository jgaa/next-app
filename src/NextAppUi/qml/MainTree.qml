import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property alias currentIndex : treeView.selectionModel.currentIndex

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

        TreeView {
            id: treeView
            property int lastIndex: -1

            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: MainTreeModel

            selectionModel: ItemSelectionModel {}

            Component.onCompleted: treeView.toggleExpanded(0)

            delegate: TreeViewDelegate {
                id: treeDelegate
                indentation: 8
                implicitWidth: treeView.width > 0 ? treeView.width : 250
                implicitHeight: 25

                // Since we have the 'ComponentBehavior Bound' pragma, we need to
                // require these properties from our model. This is a convenient way
                // to bind the properties provided by the model's role names.
                required property int index
                required property string name
                required property string uuid
                //required property nextapp.pb.Node node

                indicator: Image {
                    id: directoryIcon

                    x: treeDelegate.leftMargin + (treeDelegate.depth * treeDelegate.indentation)
                    anchors.verticalCenter: parent.verticalCenter
                    source: treeDelegate.hasChildren ? (treeDelegate.expanded
                                ? "../icons/folder_open.svg" : "../icons/folder_closed.svg")
                            : "../icons/generic_file.svg"
                    sourceSize.width: 20
                    sourceSize.height: 20
                    fillMode: Image.PreserveAspectFit

                    smooth: true
                    antialiasing: true
                    asynchronous: true
                }

                contentItem: Text {
                    text: treeDelegate.name
                    color: Colors.text
                }

                background: Rectangle {
                    color: (treeDelegate.index === treeView.lastIndex)
                        ? Colors.selection
                        : (hoverHandler.hovered ? Colors.active : "transparent")
                }

                // We color the directory icons with this MultiEffect, where we overlay
                // the colorization color ontop of the SVG icons.
                MultiEffect {
                    id: iconOverlay

                    anchors.fill: directoryIcon
                    source: directoryIcon

                    colorizationColor: (treeDelegate.expanded && treeDelegate.hasChildren)
                                             ? Colors.color2 : Colors.folder
                    colorization: 1.0
                    brightness: 1.0
                }

                HoverHandler {
                    id: hoverHandler
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onSingleTapped: (eventPoint, button) => {
                        switch (button) {
                            case Qt.LeftButton:
                                treeView.toggleExpanded(treeDelegate.row)
                                treeView.lastIndex = treeDelegate.index
                                // If this model item doesn't have children, it means it's
                                // representing a file.
                                // if (!treeDelegate.hasChildren)
                                //     root.fileClicked(treeDelegate.filePath)
                            break;
                            case Qt.RightButton:
                                // var my_uuid = treeDelegate.uuid;
                                // console.log("Tapped node uuid=", my_uuid)
                                // var node = MainTreeModel.nodeMapFromUuid(my_uuid)
                                // //var node = MainTreeModel.nodeFromUuid(my_uuid)
                                // console.log("node: ", node)
                                // console.log("Tapped node name=", node.name)

                                contextMenu.node = MainTreeModel.nodeMapFromUuid(treeDelegate.uuid)
                                contextMenu.popup();
                            break;
                        }
                    }
                }

                MyMenu {
                    id: contextMenu
                    property var node: null
                    Action {
                        text: qsTr("Edit")
                        icon.source: "../icons/fontawsome/pen-to-square.svg"
                        onTriggered: {
                            openForEditNodeDlg(contextMenu.node)
                        }
                    }
                    Action {
                        icon.source: "../icons/fontawsome/trash-can.svg"
                        text: qsTr("Delete")
                        onTriggered: {
                            confirmDelete.node = contextMenu.node
                            confirmDelete.open()
                        }
                    }
                }
            }

            // Provide our own custom ScrollIndicator for the TreeView.
            ScrollIndicator.vertical: ScrollIndicator {
                active: true
                implicitWidth: 15

                contentItem: Rectangle {
                    implicitWidth: 6
                    implicitHeight: 6

                    color: Colors.color1
                    opacity: treeView.movingVertically ? 0.5 : 0.0

                    Behavior on opacity {
                        OpacityAnimator {
                            duration: 500
                        }
                    }
                }
            }

        }
    }

    MessageDialog {
        property var node : null

        id: confirmDelete
        title: qsTr("Please confirm")
        text: qsTr("Do you really want to delete \"%1\" ?").arg(node.name)
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           MainTreeModel.deleteNode(node.uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
        }
    }

    function openDialog(name, args) {
        var component = Qt.createComponent(name);
        if (component.status !== Component.Ready) {
            if(component.status === Component.Error )
                console.debug("Error:"+ component.errorString() );
            return;
        }
        var dlg = component.createObject(root, args);
        dlg.open()
    }

    function openForEditNodeDlg(node) {
        openDialog("EditNodeDlg.qml", {
            isNew: true,
            node: node,
            title: qsTr("Edit " + node.kind)
        });
    }
}
