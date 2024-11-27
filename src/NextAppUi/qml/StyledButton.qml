import QtQuick
import QtQuick.Controls
import NextAppUi

Button {
    id: control
    text: qsTr("Button")
    property int useWidth: 100
    property int useHeight: 40

    contentItem: Text {
        text: control.text
        font: control.font
        opacity: enabled ? 1.0 : 0.3
        color: control.down ? MaterialDesignStyling.onPrimaryFixedVariant : MaterialDesignStyling.onPrimaryContainer
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: control.useWidth
        implicitHeight: control.useHeight
        opacity: enabled ? 1 : 0.3
        border.color: control.down ? MaterialDesignStyling.outline : MaterialDesignStyling.outlineVariant
        border.width: 1
        radius: 2
        color: control.down ? MaterialDesignStyling.primaryFixedDim : MaterialDesignStyling.primaryContainer
    }
}
