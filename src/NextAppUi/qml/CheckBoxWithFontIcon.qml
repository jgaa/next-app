import QtQuick
import QtQuick.Layouts

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
    property string checkedColor: Colors.icon
    property string uncheckedColor: Colors.icon
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
            font.family: (checkBox.isChecked && checkBox.useSolidForChecked) ? fontAwesomeSolid.name : fontAwesome.name
            font.styleName: (checkBox.isChecked && checkBox.useSolidForChecked) ? fontAwesomeSolid.font.styleName : fontAwesome.font.styleName
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
            color: Colors.disabledText
            //font.family: "Lato"
            font.pixelSize: 12
            Layout.alignment: Qt.AlignBottom
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            console.log("Toggeling checkbox")
            if (checkBox.autoToggle) {
                checkBox.isChecked = !checkBox.isChecked
            }
            checkBox.clicked()
        }
    }

    FontLoader { id: fontAwesome; source: "../fonts/Font Awesome 6 Free-Regular-400.otf" }
    FontLoader { id: fontAwesomeSolid; source: "../fonts/Font Awesome 6 Free-Solid-900.otf" }
}
