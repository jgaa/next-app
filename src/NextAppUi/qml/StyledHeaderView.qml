import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

HorizontalHeaderView {
    id: control
    clip: true

    delegate: Item {
        required property string display
        implicitHeight: 30

        Rectangle {
            anchors.fill: parent
            color: MaterialDesignStyling.primary
            border {
                color: MaterialDesignStyling.outline
                width: 1
            }
        }

        Label {
            text: display
            color: MaterialDesignStyling.onPrimary
            anchors.centerIn: parent
        }
    }
}
