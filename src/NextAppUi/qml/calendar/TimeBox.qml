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

    Rectangle {
        id: dragUpRect
        visible: root.haveDragIcons
        anchors.left : parent.left
        anchors.top: parent.top
        height: 14
        width: 14
        radius: 50
        color: MaterialDesignStyling.primaryFixed

        Text {
            anchors.fill: parent
            font.family: ce.faSolidName
            font.styleName: ce.faSolidStyle
            font.pixelSize: 10
            text: "\uf0de"
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            color: MaterialDesignStyling.onPrimary
        }
    }

    MouseArea {
        anchors.fill: dragUpRect
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        property real startY: 0
        property real origY: 0
        property real origH: 0

        onPressed: {
            cursorShape = Qt.SizeVerCursor
            let parentPos = mapToItem(root.parent, mouseX, mouseY)
            startY = parentPos.y
            origY = root.y
            origH = root.height
        }

        onPositionChanged: {
            let parentPos = mapToItem(root.parent, mouseX, mouseY)
            let delta = startY - parentPos.y
            let snappedDelta = Math.round(delta / (minuteHight * 5.0)) * (minuteHight * 5.0) // Snap to 5-minute intervals
            if (root.height + snappedDelta > 20) {
                root.height = origH + snappedDelta
                root.y = origY - snappedDelta
            }
        }

        onReleased: {
            cursorShape = Qt.ArrowCursor
            let parentPos = mapToItem(root.parent, mouseX, mouseY)
            let hour = Math.floor(parentPos.y / 60)
            let minute = Math.floor((parentPos.y % 60) / (minuteHight * 5.0)) * (minuteHight * 5.0)
            console.log("released at x=", parentPos.x, ", y=", parentPos.y,
                        " hour=", hour, "minute=", minute)

            root.y = origY
            root.height = origH

            let start_time = new Date(root.model.year, root.model.month - 1, root.model.day)
            start_time.setHours(hour)
            start_time.setMinutes(minute)

            console.log("start_time is ", start_time, ", start is ", root.start)

            root.model.moveEvent(root.uuid, Math.floor(start_time.getTime() / 1000), 0)

            console.log("released")
        }
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
