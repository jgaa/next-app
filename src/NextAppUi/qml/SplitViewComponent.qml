import QtQuick
import QtQuick.Controls
import NextAppUi

Rectangle {
    id: root
    height: 12
    color: dragHandler.active ? Colors.iconIndicator : "yellow"
    clip: false
    z: 1000

    Rectangle {
        width: 58
        height: 26
        radius: height / 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        color: dragHandler.active ? Colors.iconIndicator : Colors.icon
        opacity: dragHandler.active || hoverHandler.hovered ? 0.95 : 0.55

        Rectangle {
            width: 34
            height: 4
            radius: 2
            anchors.centerIn: parent
            color: "yellow"
            opacity: 0.85
        }
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: Qt.SizeVerCursor
    }

    Item {
        id: dragHandler
        width: parent.width
        height: 48
        anchors.centerIn: parent

        readonly property bool active: pressed

        property bool pressed: false

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor

            onPressed: function(mouse) {
                dragHandler.pressed = true
                const p = dragHandler.mapToItem(root.parent, mouse.x, mouse.y)
                root.parent.setSplitPosition(p.y)
            }

            onReleased: dragHandler.pressed = false
            onCanceled: dragHandler.pressed = false

            onPositionChanged: function(mouse) {
                if (pressed) {
                    const p = dragHandler.mapToItem(root.parent, mouse.x, mouse.y)
                    root.parent.setSplitPosition(p.y)
                }
            }
        }
    }
}
