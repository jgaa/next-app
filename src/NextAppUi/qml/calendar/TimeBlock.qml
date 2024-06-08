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
    property string name
    property string uuid
    property string start
    property string end
    required property var model

    property bool haveDragIcons: true // height > 20 && width > 50
    property real minuteHight: parent.hourHeight / 60.0

    DragHandler {
        id: dragHandler
        target: root
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
    }

    //Drag.active: dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.MoveAction
    Drag.mimeData: {
        "text/app.nextapp.calendar.event": root.uuid
    }

    ExpandArea {
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
    }

    ColumnLayout {
        anchors.fill: parent

        Text {
            color: MaterialDesignStyling.onPrimary
            text: root.start
        }

        Text {
            color: MaterialDesignStyling.onPrimary
            text: root.name
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
