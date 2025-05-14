import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models
import "../common.js" as Common

Rectangle {
    id: root
    color: MaterialDesignStyling.primary
    radius: 6
    border.color: MaterialDesignStyling.outline
    border.width: 1
    opacity: 0.8
    clip: true
    property string name
    property string uuid
    property string start
    property string end
    property string category
    property string duration
    required property var model

    property bool haveDragIcons: true // height > 20 && width > 50
    property real minuteHight: parent.hourHeight / 60.0

    DragHandler {
        id: dragHandler
        target: root
        enabled: NaCore.dragEnabled
        property real origX: root.x
        property real origY: root.y

        onActiveChanged: {
            if (active) {
                root.opacity = 0.5
                dragHandler.origX = root.x
                dragHandler.origY = root.y
                root.grabToImage(function(result) {
                   parent.Drag.imageSource = result.url
                   parent.Drag.active = true
                })
            } else {
                root.opacity = 0.8
                root.x = dragHandler.origX
                root.y = dragHandler.origY
                parent.Drag.active = false
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: contextMenu.popup()

    }

    TapHandler {
        onLongPressed: contextMenu.popup()

        onTapped: {
            // Start drag
            if (NaCore.dragEnabled) {
                dragHandler.active = true
            }
        }
    }

    // MouseArea {
    //     anchors.fill: parent
    //     drag.target: root
    // }

    DropArea {
        id: dropArea
        anchors.fill: parent
        onEntered: function(drag) {
            // console.log("TimeBlock: DropArea entered by ", drag.source.toString(), " types ", drag.formats)

            if (drag.formats.indexOf("text/app.nextapp.action") !== -1) {

                var uuid = drag.getDataAsString("text/app.nextapp.action")

                // TODO: Check if the action is already present
                drag.accepted = true
                return
            }
        }

        onDropped: function(drop) {
            // console.log("DropArea receiceived a drop! source=", drop.source.uuid)

            if (drop.formats.indexOf("text/app.nextapp.action") !== -1) {

                var uuid = drop.getDataAsString("text/app.nextapp.action")
                drop.accepted = root.model.addAction(root.uuid, uuid)
                return
            }

            drop.accepted = false
        }
    }


    //Drag.active: dragHandler.active
    Drag.dragType: NaCore.dragEnabled ? Drag.Automatic : Drag.None
    Drag.supportedActions: Qt.MoveAction
    Drag.mimeData: {
        "text/app.nextapp.calendar.event": root.uuid
    }

    Rectangle {
        id: cat
        color: NaAcModel.valid ? NaAcModel.getColorFromUuid(root.category) : "transparent"
        width: expandAreaTop.width
        height: parent.height
        radius: 5
    }

    ExpandArea {
        id: expandAreaTop
        target: root
        directionUp: true
        anchors.left : root.left
        anchors.top: root.top
        visible: root.haveDragIcons
    }

    ExpandArea {
        target: root
        directionUp: false
        anchors.right : root.right
        anchors.bottom: root.bottom
        visible: root.haveDragIcons
        z: actionsCtl.z + 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        RowLayout {
            id: header
            Layout.fillWidth: true
            Item {
                // Upper left expand icon
                Layout.preferredWidth: expandAreaTop.width + 2
            }

            Text {
                color: MaterialDesignStyling.onPrimary
                text: root.start + " - " + root.end
            }

            Text {
                color: MaterialDesignStyling.onPrimary
                text: root.name
                font.bold: true
                //font.pointSize: 14
            }

            Item {
                Layout.fillWidth: true
            }
        }


        ListView {
            id: actionsCtl
            //Layout.fillHeight: true
            Layout.leftMargin: expandAreaTop.width
            Layout.preferredHeight: contentHeight
            Layout.minimumHeight: 0
            Layout.fillWidth: true
            interactive: false
            clip: true
            model: root.model?.valid ? root.model.getTimeBoxActionsModel(root.uuid, root) : null

            delegate: Rectangle {
                id: actionItem
                implicitHeight: actionItemLayout.implicitHeight
                implicitWidth: actionsCtl.width
                color: index % 2 ? MaterialDesignStyling.onPrimary : MaterialDesignStyling.primaryContainer
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property string category
                required property bool workedOnToday

                RowLayout {
                    id: actionItemLayout

                    Rectangle {
                        height: nameCtl.implicitHeight
                        width: 10
                        color: NaAcModel.valid ? NaAcModel.getColorFromUuid(category) : "transparent"
                        //radius: 5
                    }

                    // Icon for done
                    Text {
                        font.family: ce.faNormalName
                        font.pointSize: nameCtl.font.pointSize
                        text: done ? "\uf058" : "\uf111"
                        color: done ? "green" : workedOnToday ? "dodgerblue" : "orange"

                        Rectangle {
                            color: "white"
                            anchors.fill: parent
                            radius: 100
                            z: parent.z -1
                            visible: done
                        }
                    }

                    Text {
                        id: nameCtl
                        text: name
                        color: MaterialDesignStyling.onPrimaryContainer
                    }
                }
            }

            // Rectangle {
            //     anchors.fill: parent
            //     z: parent.z - 1
            //     color: MaterialDesignStyling.primaryContainer
            //     radius: 5
            // }
        }

        Item {
            Layout.fillHeight: true
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Layout.leftMargin: expandAreaTop.width
            text: qsTr("Duration ") + root.duration
            color: MaterialDesignStyling.onPrimary
            visible: parent.height - header.height - actionsCtl.height >= height - 2
        }
    }

    CommonElements {
        id: ce
    }

    MyMenu {
        id: contextMenu

        Action {
            text: qsTr("Delete")
            onTriggered: {
                root.model.deleteEvent(root.uuid)
            }
        }

        Action {
            text: qsTr("Edit")
            onTriggered: {
                openTimeBlockDlg()
            }
        }
    }

    function openTimeBlockDlg() {
        Common.openDialog("calendar/EditTimeBlockDlg.qml", root, {
            title: qsTr("Edit Time Block"),
            tb: root.model.tbById(root.uuid)
        });
    }
}
