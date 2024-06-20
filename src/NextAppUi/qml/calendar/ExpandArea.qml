import QtQuick
import QtQuick.Controls
import NextAppUi

Item {
    id: root
    height: 14
    width: 14

    property bool directionUp: true
    property Item target: null

    Rectangle {
        anchors.fill: parent
        id: dragUpRect
        radius: 50
        color: MaterialDesignStyling.primaryFixed

        Text {
            anchors.fill: parent
            font.family: ce.faSolidName
            font.styleName: ce.faSolidStyle
            font.pixelSize: 10
            text: directionUp ? "\uf0de" : "\uf0d7"
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            color: MaterialDesignStyling.onPrimary
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        property real startY: 0
        property real origY: 0
        property real origH: 0

        onPressed: {
            cursorShape = Qt.SizeVerCursor
            let parentPos = mapToItem(target.parent, mouseX, mouseY)
            startY = parentPos.y
            origY = target.y
            origH = target.height
        }

        onPositionChanged: {
            let parentPos = mapToItem(target.parent, mouseX, mouseY)
            let delta = startY - parentPos.y
            let snappedDelta = Math.floor(delta)

            if (target.height + snappedDelta > 20) {
                if (directionUp) {
                    target.height = origH + snappedDelta
                    target.y = origY - snappedDelta
                } else {
                    target.height = origH - snappedDelta
                }
            }
        }

        onReleased: {
            cursorShape = Qt.ArrowCursor
            let parentPos = mapToItem(target.parent, mouseX, mouseY)
            let hour = Math.floor(parentPos.y / 60)
            let minute = getRoundedMinutes(parentPos.y % 60)
            // console.log("released at x=", parentPos.x, ", y=", parentPos.y,
            //            " hour=", hour, "minute=", minute)

            target.y = origY
            target.height = origH

            let new_time = new Date(target.model.when * 1000)
            new_time.setHours(hour)
            new_time.setMinutes(minute)

            if (directionUp) {
                // console.log("start_time is ", new_time, ", start is ", target.start)
                target.model.moveEvent(target.uuid, new_time.getTime() / 1000, 0)
            } else {
                // console.log("end_time is ", new_time, ", end is ", target.end)
                target.model.moveEvent(target.uuid, 0, new_time.getTime() / 1000)
            }

            // console.log("released")
        }
    }

    function getRoundedMinutes(minute) {
        const rounded = Math.round(minute / target.model.roundToMinutes) * target.model.roundToMinutes
        // console.log("minute=", minute, ", rounded=", rounded, ", roundToMinutes=", target.model.roundToMinutes)
        return rounded
    }
}
