// SliderTicks.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls


RowLayout{
    id: root

    Layout.fillWidth: true
    Label {
        text: qsTr("Low")
        font.pixelSize: 10
        color: "green"
    }

    Item {
        Layout.fillWidth: true
    }

    Label {
        text: qsTr("Medium")
        font.pixelSize: 10
        color: "orange"
    }

    Item {
        Layout.fillWidth: true
    }

    Label {
        text: qsTr("High")
        font.pixelSize: 10
        color: "red"
    }
}

// Item {
//     id: root
//     Layout.fillWidth: true   // <-- important if used inside a ColumnLayout
//     height: 20               // fixed small height

//     property int count: 11         // Number of tick positions (e.g., 11 for 0-10)
//     property int majorStep: 5      // Major tick every N steps
//     property real tickOpacity: 0.4
//     property color tickColor: "black"

//     Repeater {
//         model: count

//         Item {
//             width: 1
//             height: parent.height
//             anchors.verticalCenter: parent.verticalCenter

//             Rectangle {
//                 width: (index % root.majorStep === 0) ? 2 : 1
//                 height: (index % root.majorStep === 0) ? parent.height : parent.height * 0.5
//                 color: root.tickColor
//                 opacity: root.tickOpacity
//                 anchors.horizontalCenter: parent.horizontalCenter
//                 anchors.verticalCenter: parent.verticalCenter
//             }

//             Label {
//                 visible: index === 0 || index === Math.floor(count/2) || index === count-1
//                 text: {
//                     if (index === 0) return qsTr("Low")
//                     if (index === Math.floor(count/2)) return qsTr("Medium")
//                     if (index === count-1) return qsTr("High")
//                     return ""
//                 }
//                 font.pixelSize: 10
//                 anchors.top: parent.bottom
//                 anchors.horizontalCenter: parent.horizontalCenter
//                 anchors.topMargin: 2
//             }

//             // dynamic x position
//             x: (root.width - width) * (index / (model - 1))
//         }
//     }

//     Behavior on width {
//         NumberAnimation { duration: 200 }
//     }
// }
