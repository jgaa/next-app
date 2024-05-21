import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Rectangle {
    id: root
    color: MaterialDesignStyling.primaryContainer
    radius: 6
    border.color: MaterialDesignStyling.outline
    border.width: 1
    opacity: 0.8
    property string name : "Unknown"

    ColumnLayout {
        anchors.fill: parent

        Text {
            color: MaterialDesignStyling.onPrimaryContainer
            text: root.name
        }
    }

    onVisibleChanged: {
        console.log("TimeBox visible: ", root.visible)
    }
}
