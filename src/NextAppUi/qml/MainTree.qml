import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    //property alias currentIndex : treeView.selectionModel.currentIndex
    property string selectedItemUuid: MainTreeModel.selected
    property bool hasSelection: selectedItemUuid !== ""

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

        ScrollView {
            Layout.fillHeight: true
            Layout.fillWidth: true

            TreeView {
                id: treeView
                property int lastIndex: -1

                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                clip: true
                // Layout.fillHeight: true
                // Layout.fillWidth: true
                anchors.fill: parent
                model: MainTreeModel
                //rootIndex: MainTreeModel.useRoot

                //selectionModel: ItemSelectionModel { id: selection }

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
                    required property string kind

                    //Drag.dragType:

                    indicator: Image {
                        id: treeIcon

                        x: treeDelegate.leftMargin + (treeDelegate.depth * treeDelegate.indentation)
                        anchors.verticalCenter: parent.verticalCenter
                        source: treeDelegate.hasChildren ? (treeDelegate.expanded
                                    ? "../icons/fontawsome/angle-down.svg" : "../icons/fontawsome/angle-right.svg")
                                : "../icons/fontawsome/circle.svg"
                        sourceSize.width: 12
                        sourceSize.height: 12
                        fillMode: Image.PreserveAspectFit

                        smooth: true
                        antialiasing: true
                        asynchronous: true
                        visible: treeDelegate.hasChildren
                    }

                    MultiEffect {
                        id: iconOverlay

                        anchors.fill: treeIcon
                        source: treeIcon

                        colorizationColor: (treeDelegate.expanded && treeDelegate.hasChildren)
                                                 ? Colors.color2 : Colors.color1
                        colorization: 1.0
                        brightness: 1.0
                        visible: treeDelegate.hasChildren
                    }

                    contentItem: RowLayout {
                        id: content
                        spacing: 2

                        Item {
                            width: 20
                            height: 16

                            Image {
                                id: kindIcon
                                Layout.preferredWidth: 16
                                Layout.fillWidth: false
                                source:  "../icons/" + treeDelegate.kind + ".svg"
                                sourceSize.width: 16
                                sourceSize.height: 16
                                fillMode: Image.PreserveAspectFit

                                Layout.alignment: Qt.AlignLeft
                            }

                            MultiEffect {
                                anchors.fill: kindIcon
                                source: kindIcon

                                colorizationColor: Colors.text
                                colorization: 1.0
                                brightness: 1.0
                            }

                            Drag.onDragStarted: {
                                console.log("Drag started")
                            }
                        }

                        Text {
                            text: treeDelegate.name
                            color: Colors.text
                            id: label
                            clip: true
                            Layout.alignment: Qt.AlignLeft
                            Layout.fillWidth: true
                        }
                    }

                    Drag.active: dragHandler.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.MoveAction
                    Drag.mimeData: {
                        "text/app.nextapp.node": treeDelegate.uuid
                    }

                    DragHandler {
                        id: dragHandler
                        target: content
                        onActiveChanged: {
                            console.log("DragHandler: active=", active)
                        }
                    }

                    background: Rectangle {
                        color: (MainTreeModel.selected === treeDelegate.uuid)
                            ? Colors.selection
                            : (hoverHandler.hovered ? Colors.active : "transparent")
                    }

                    DropArea {
                        id: dropArea
                        anchors.fill: parent

                        onEntered: function(drag) {
                            console.log("DropArea entered by ", drag.source.toString(), " types ", drag.formats)

                            if (drag.formats.indexOf("text/app.nextapp.action") !== -1) {

                                var uuid = drag.getDataAsString("text/app.nextapp.action")
                                var currNode = drag.getDataAsString("text/app.nextapp.curr.node")

                                console.log("Drag from Action: curr-node=", currNode, " action=", uuid,
                                            ", drop node=", treeDelegate.uuid)

                                if (currNode === treeDelegate.uuid) {
                                    console.log("We should not allow the drop here. Same node.")
                                    drag.accepted = false
                                    return
                                }
                                drag.accepted = true

                                return
                            }

                            if (drag.formats.indexOf("text/app.nextapp.node") !== -1) {
                                console.log("text/app.nextapp.node: ",
                                            drag.getDataAsString("text/app.nextapp.node"))

                                var uuid = drag.getDataAsString("text/app.nextapp.node")
                                if (!MainTreeModel.canMove(uuid, treeDelegate.uuid)) {
                                    console.log("We should not allow the drop here")
                                    drag.accepted = false
                                }
                                drag.accepted = true
                                return
                            }
                        }

                        onDropped: function(drop) {
                            console.log("DropArea receiceived a drop! source=", drop.source.uuid)

                            if (drop.formats.indexOf("text/app.nextapp.action") !== -1) {

                                var uuid = drop.getDataAsString("text/app.nextapp.action")
                                var currNode = drop.getDataAsString("text/app.nextapp.curr.node")
                                drop.accepted = ActionsModel.moveToNode(uuid, treeDelegate.uuid)
                                return
                            }

                            if (drop.formats.indexOf("text/app.nextapp.node") !== -1) {
                                console.log("text/app.nextapp.node: ",
                                            drop.getDataAsString("text/app.nextapp.node"))

                                var uuid = drop.getDataAsString("text/app.nextapp.node")
                                if (MainTreeModel.canMove(drop.source.uuid, treeDelegate.uuid)) {
                                    console.log("Seems OK")
                                    drop.accepted = true
                                    MainTreeModel.moveNode(drop.source.uuid, treeDelegate.uuid)
                                    return
                                }
                            }

                            drop.accepted = false
                        }
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
                                    setSelection(treeDelegate.uuid)
                                break;
                                case Qt.RightButton:
                                    contextMenu.node = MainTreeModel.nodeMapFromUuid(treeDelegate.uuid)
                                    contextMenu.index = treeDelegate.index
                                    contextMenu.popup();
                                break;
                            }
                        }

                        onLongPressed: {
                            contextMenu.node = MainTreeModel.nodeMapFromUuid(treeDelegate.uuid)
                            contextMenu.popup();
                        }
                    }

                    MyMenu {
                        id: contextMenu
                        property var node: null
                        property int index: -1
                        Action {
                            text: qsTr("Expand")
                            onTriggered: {
                                treeView.expandRecursively(index, -1)
                            }
                        }
                        Action {
                            text: qsTr("Collapse")
                            onTriggered: {
                                treeView.collapseRecursively(index)
                            }
                        }
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
            }
        }
    }

    MessageDialog {
        property var node : null

        id: confirmDelete
        title: qsTr("Do you really want to delete \"%1\" ?").arg(node.name)
        text: qsTr("Note that any sub-items and all related information, including worked time, project information, actions etc. will also be deleted! This action can not be undone.")
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

    function setSelection(uuid) {
        console.log("Selection changed to ", uuid)
        MainTreeModel.selected = uuid;
    }
}
