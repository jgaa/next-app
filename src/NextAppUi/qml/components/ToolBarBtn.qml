import QtQuick
import QtQuick.Controls
import NextAppUi

Rectangle {
    id: root
    height: 28
    width: 28
    radius: 5
    property bool isSolid: true
    property alias icon : iconCtl.text
    property alias iconColor: iconCtl.color
    property bool isActive: false
    property string text
    property string tooltipText: ""
    color: "transparent"
    signal clicked

    Text {
        id: iconCtl
        font.family: root.isSolid ? ce.faSolidName : ce.faNormalName
        font.styleName: root.isSolid ? ce.faSolidStyle : ce.faNormalStyle
        font.pixelSize: 14
        color: root.isActive ? MaterialDesignStyling.onPrimaryContainer : MaterialDesignStyling.secondaryContainer
        anchors.centerIn: parent
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        onEntered: {
            if (root.isActive) {
                root.color = MaterialDesignStyling.primaryContainer
            }
        }

        onExited: {
            root.color = "transparent"
        }

        onClicked: {
            if (root.isActive) {
                clickAnimation.start();
                root.clicked()
            }
        }
    }

    SequentialAnimation {
        id: clickAnimation
        running: false
        PropertyAnimation { target: root; property: "scale"; to: 0.9; duration: 80; easing.type: Easing.OutQuad }
        PropertyAnimation { target: root; property: "scale"; to: 1.0; duration: 120; easing.type: Easing.OutBounce }
    }

    ToolTip.visible: mouseArea.containsMouse && root.tooltipText.length > 0
    ToolTip.text: root.tooltipText
    ToolTip.delay: 500
    ToolTip.timeout: 3000

    CommonElements {
        id: ce
    }
}

