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
    property bool autoToggle: true
    width: childrenRect.width
    height: childrenRect.height
    color: "transparent"
    signal clicked

    RowLayout
    {
        spacing: 5

        // Checkbox icons
        Text {
            id: icon
            font.family: (checkBox.isChecked && checkBox.useSolidForChecked) ? ce.faSolidName : ce.faNormalName
            font.styleName: (checkBox.isChecked && checkBox.useSolidForChecked) ? ce.faSolidStyle : ce.faNormalStyle
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
            //font.family: "Lato"
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

    // FontLoader { id: fonts.normal; source: "../fonts/Font Awesome 6 Free-Regular-400.otf" }
    // FontLoader { id: fonts.solid; source: "../fonts/Font Awesome 6 Free-Solid-900.otf" }
}
