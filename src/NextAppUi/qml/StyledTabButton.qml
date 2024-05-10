import QtQuick
import QtQuick.Controls
import NextAppUi

TabButton {
    id: control

    implicitHeight: parent.height -2

    background: Rectangle {
        color: control.checked ? MaterialDesignStyling.primaryContainer : MaterialDesignStyling.primary
        radius: 2
        // border {
        //     color: MaterialDesignStyling.outline
        //     width: 1
        // }

        Rectangle {
            anchors {
                top: parent.top + 2
                bottom: parent.bottom - 2
                left: parent.left + 2
            }
            width: 2
            color: MaterialDesignStyling.scrim
            opacity: control.checked ? 0.5 : 0.2
        }
    }

    contentItem: Text {
        color: control.checked ? MaterialDesignStyling.onPrimaryContainer : MaterialDesignStyling.onPrimary
        text: control.text
        verticalAlignment: Text.AlignVCenter
    }
}
