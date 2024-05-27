import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

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

    // MouseArea {
    //     id: mouseCtl
    //     drag.target: parent
    //     anchors.fill: parent
    //     property var origX: root.x
    //     property var origY: root.y

    //     onEntered: {
    //         mouseCtl.origX = root.x
    //         mouseCtl.origY = root.y
    //     }

    //     onReleased: {
    //         root.x = mouseCtl.origX
    //         root.y = mouseCtl.origY
    //     }
    // }

    DragHandler {
        id: dragHandler
        target: root
        property var origX: root.x
        property var origY: root.y

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
}
