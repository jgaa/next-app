import QtQuick
import QtQuick.Layouts
import NextAppUi

// https://medium.com/@eduard.metzger/how-to-make-a-quick-custom-qml-checkbox-using-icon-fonts-b2ffbd651144
// with modifications
Rectangle
{
    id: checkBox
    property bool isChecked: false
    property alias text: label.text
    property int iconSize: 20
    property string checkedCode: "\uf14a"
    property string uncheckedCode: "\uf0c8"
    property string checkedColor: MaterialDesignStyling.onSurface
    property string uncheckedColor: MaterialDesignStyling.onSurface
    property string textColor: MaterialDesignStyling.onSurfaceVariant
    property string selectedBackgroundColor: MaterialDesignStyling.surfaceContainerHighest
    property bool useSolidForChecked: false
    property bool useSolidForAll: false
    property bool autoToggle: true
    property string bgColor: "transparent"
    property bool attentionAnimation: false

    width: childrenRect.width
    height: childrenRect.height
    color: bgColor
    signal clicked

    RowLayout
    {
        spacing: 5

        // Checkbox icons
        Text {
            id: icon
            property bool isSolid: checkBox.useSolidForAll || (checkBox.isChecked && checkBox.useSolidForChecked)
            font.family: isSolid ? ce.faSolidName : ce.faNormalName
            font.styleName: isSolid ? ce.faSolidStyle : ce.faNormalStyle
            font.pixelSize: checkBox.iconSize
            text: checkBox.isChecked ? checkBox.checkedCode : checkBox.uncheckedCode
            color: checkBox.isChecked ? checkBox.checkedColor : checkBox.uncheckedColor
            width: checkBox.iconSize
            height: checkBox.iconSize
        }

        // Checkbox text
        Text
        {
            id: label
            visible: text !== ""
            color: checkBox.textColor
            font.pixelSize: 12
            Layout.alignment: Qt.AlignBottom
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true

        onEntered: {
            checkBox.color = checkBox.selectedBackgroundColor
        }

        onExited: {
            checkBox.color = "transparent"
        }

        onClicked: {
            // console.log("Toggeling checkbox")
            if (checkBox.autoToggle) {
                checkBox.isChecked = !checkBox.isChecked
            }
            checkBox.clicked()
        }
    }

    CommonElements {
        id: ce
    }

    // Add inside the Rectangle scope (top-level component)
    SequentialAnimation on scale {
        running: checkBox.attentionAnimation
        loops: Animation.Infinite
        alwaysRunToEnd: false

        NumberAnimation {
            target: icon
            property: "scale"
            to: 1.3
            duration: 150
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: icon
            property: "scale"
            to: 1.0
            duration: 150
            easing.type: Easing.InOutQuad
        }
    }

}
