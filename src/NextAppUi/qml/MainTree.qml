import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi
import Nextapp.Models

pragma ComponentBehavior: Bound

Rectangle {
    id: root
    //anchors.fill: parent
    enabled: NaComm.connected && NaMainTreeModel.useRoot

    DisabledDimmer {}

    //property alias currentIndex : treeView.selectionModel.currentIndex
    property string selectedItemUuid: NaMainTreeModel.selected
    property bool hasSelection: selectedItemUuid !== ""

    color: MaterialDesignStyling.surface

    ColumnLayout {
        anchors.fill: parent

        // ScrollView {
        //     Layout.fillHeight: true
        //     Layout.fillWidth: true
        //     contentWidth: width
        //     contentHeight: treeView.height

            TreeView {
                id: treeView
                property int lastIndex: -1
                property bool wasExpanded: false
                Layout.fillHeight: true
                Layout.fillWidth: true

                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                clip: true
                //anchors.fill: parent
                model: NaMainTreeModel

                Connections {
                    target: treeView.model

                    function onUseRootChanged() {
                        console.log("onUseRootChanged: Tree Model was reset. It has ", treeView.model.rowCount(), " items")
                        treeView.expandRecursively(-1, 2)
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: vScrollBar
                    parent: treeView
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: MaterialDesignStyling.scrollBarWidth
                    policy: ScrollBar.AlwaysOn
                }


                delegate: TreeViewDelegate {
                    id: treeDelegate
                    indentation: 12
                    implicitWidth: treeView.width - MaterialDesignStyling.scrollBarWidth
                    implicitHeight: 25

                    required property int index
                    required property string name
                    required property string uuid
                    required property string kind

                    indicator: Text {
                        x: treeDelegate.leftMargin + (treeDelegate.depth * treeDelegate.indentation) + 10
                        //width: 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: MaterialDesignStyling.onSurfaceVariant
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pointSize: 14
                        text: treeDelegate.hasChildren
                              ? treeDelegate.expanded ? "\uf0d7" : "\uf0da" : ""
                    }

                    TapHandler {
                        target: indicator
                        acceptedButtons: Qt.LeftButton
                        onSingleTapped: (eventPoint, button) => {
                            const exclude = eventPoint.pressPosition.x > indicator.x + 40
                            console.log("indicator tapped, indicator.x ", indicator.x, " exclude ", exclude )
                            if (!exclude) {
                                treeView.toggleExpanded(treeDelegate.row)
                            }
                        }
                        // onDoubleTapped: {
                        //     console.log("indicator double tapped")
                        //     treeView.toggleExpanded(treeDelegate.row)
                        // }
                    }

                    contentItem: RowLayout {
                        id: content
                        spacing: 2

                        Item {
                            Layout.leftMargin: 10
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 20

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

                                colorizationColor: MaterialDesignStyling.onSurface
                                colorization: 1.0
                                brightness: 1.0
                            }

                            Drag.onDragStarted: {
                                // console.log("Drag started")
                            }
                        }

                        Text {
                            id: label
                            text: treeDelegate.name
                            color: MaterialDesignStyling.onSurface
                            clip: true
                            Layout.alignment: Qt.AlignLeft
                            Layout.fillWidth: true
                        }

                            TapHandler {
                                target: treeDelegate
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onSingleTapped: (eventPoint, button) => {
                                    const exclude = eventPoint.pressPosition.x >= label.x
                                    console.log("TapHandler: ", name, ", row=", treeDelegate.row, "button=", button,
                                        " expanded ", treeView.isExpanded(treeDelegate.row),
                                        " exclude ", exclude,
                                        " label.x ", label.x,
                                        " contains", label.contains(eventPoint.pressPosition),
                                        " eventPoint.pressPosition.x=", eventPoint.pressPosition.x,
                                        " evenPoint=", eventPoint)
                                    switch (button) {
                                        case 0: // touch
                                        case Qt.LeftButton:
                                            treeView.lastIndex = treeDelegate.index
                                            setSelection(uuid)
                                            // //if (!exclude /*label.contains(eventPoint.pressPosition)*/) {
                                            //     // Buggy
                                            //     //treeView.toggleExpanded(treeDelegate.row)
                                            //     console.log("Single tapped / toggle! ")
                                            //     treeView.expand(!treeView.isExpanded(treeDelegate.row))
                                            // //}
                                        break;
                                        case Qt.RightButton:
                                            contextMenu.node = NaMainTreeModel.nodeMapFromUuid(uuid)
                                            contextMenu.index = treeDelegate.index
                                            contextMenu.popup();
                                        break;
                                    }
                                }

                                // onDoubleTapped: {
                                //     console.log("Double tapped: ", name, ", row=", treeDelegate.row, " expanded: ", treeView.isExpanded(treeDelegate.row))
                                //     // Buggy
                                //     treeView.toggleExpanded(treeDelegate.row)
                                //     //treeView.expand(!treeView.isExpanded(treeDelegate.row))
                                // }

                                onLongPressed: {
                                    contextMenu.node = NaMainTreeModel.nodeMapFromUuid(treeDelegate.uuid)
                                    contextMenu.popup();
                                }
                            }
                    }

                    Drag.active: dragHandler.active
                    Drag.dragType: NaCore.isMobile ? Drag.None :  Drag.Automatic
                    Drag.supportedActions: Qt.MoveAction
                    Drag.mimeData: {
                        "text/app.nextapp.node": treeDelegate.uuid
                    }

                    DragHandler {
                        id: dragHandler
                        target: content
                        enabled: !NaCore.isMobile
                        onActiveChanged: {
                            // console.log("DragHandler: active=", active)
                        }
                    }

                    background: Rectangle {
                        id: bg
                        property bool isSelected : NaMainTreeModel.selected === treeDelegate.uuid

                        color: (isSelected)
                            ? MaterialDesignStyling.surfaceContainer
                            : (hoverHandler.hovered ? MaterialDesignStyling.surfaceContainerHighest : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            SelectedIndicatorBar {
                                selected: bg.isSelected
                            }
                        }
                    }

                    DropArea {
                        id: dropArea
                        anchors.fill: parent

                        onEntered: function(drag) {
                            // console.log("DropArea entered by ", drag.source.toString(), " types ", drag.formats)

                            if (drag.formats.indexOf("text/app.nextapp.action") !== -1) {

                                var uuid = drag.getDataAsString("text/app.nextapp.action")
                                var currNode = drag.getDataAsString("text/app.nextapp.curr.node")

                                // console.log("Drag from Action: curr-node=", currNode, " action=", uuid,
                                //            ", drop node=", treeDelegate.uuid)

                                if (currNode === treeDelegate.uuid) {
                                    // console.log("We should not allow the drop here. Same node.")
                                    drag.accepted = false
                                    return
                                }
                                drag.accepted = true

                                return
                            }

                            if (drag.formats.indexOf("text/app.nextapp.node") !== -1) {
                                // console.log("text/app.nextapp.node: ",
                                //            drag.getDataAsString("text/app.nextapp.node"))

                                var uuid = drag.getDataAsString("text/app.nextapp.node")
                                if (!NaMainTreeModel.canMove(uuid, treeDelegate.uuid)) {
                                    // console.log("We should not allow the drop here")
                                    drag.accepted = false
                                }
                                drag.accepted = true
                                return
                            }
                        }

                        onDropped: function(drop) {
                            // console.log("DropArea receiceived a drop! source=", drop.source.uuid)

                            if (drop.formats.indexOf("text/app.nextapp.action") !== -1) {

                                var uuid = drop.getDataAsString("text/app.nextapp.action")
                                var currNode = drop.getDataAsString("text/app.nextapp.curr.node")
                                drop.accepted = ActionsModel.moveToNode(uuid, treeDelegate.uuid)
                                return
                            }

                            if (drop.formats.indexOf("text/app.nextapp.node") !== -1) {
                                // console.log("text/app.nextapp.node: ",
                                //            drop.getDataAsString("text/app.nextapp.node"))

                                var uuid = drop.getDataAsString("text/app.nextapp.node")
                                if (NaMainTreeModel.canMove(drop.source.uuid, treeDelegate.uuid)) {
                                    // console.log("Seems OK")
                                    drop.accepted = true
                                    NaMainTreeModel.moveNode(drop.source.uuid, treeDelegate.uuid)
                                    return
                                }
                            }

                            drop.accepted = false
                        }
                    }

                    HoverHandler {
                        id: hoverHandler
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
        //}
    }

    MessageDialog {
        property var node : null

        id: confirmDelete
        title: node ?  qsTr("Do you really want to delete \"%1\" ?").arg(node.name) : ""
        text: qsTr("Note that any sub-items and all related information, including worked time, project information, actions etc. will also be deleted! This action can not be undone.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           NaMainTreeModel.deleteNode(node.uuid)
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
        // console.log("Selection changed to ", uuid)
        NaMainTreeModel.selected = uuid;
    }

    CommonElements {
        id: ce
    }
}
