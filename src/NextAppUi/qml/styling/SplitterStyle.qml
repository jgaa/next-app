import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models

Rectangle {
    property  bool vertical: false
    implicitWidth: vertical ? parent.width : 10
    implicitHeight: vertical ? 10 : parent.height
    color: SplitHandle.pressed ? MaterialDesignStyling.primary : MaterialDesignStyling.surfaceContainerHigh
    border.color: SplitHandle.hovered ? MaterialDesignStyling.outline : MaterialDesignStyling.surfaceContainer
    opacity: SplitHandle.hovered || navigationView.width < 15 ? 1.0 : 0.3

    Behavior on opacity {
        OpacityAnimator {
            duration: 1400
        }
    }
}
