import QtQuick
import QtQuick.Layouts
import QtQuick.Controls


Rectangle {
    property alias text: input.text
    property int pwidth: 300
    Layout.fillHeight: false
    Layout.fillWidth: false
    Layout.preferredHeight: font.pixelSize + 12
    Layout.preferredWidth: pwidth
    Layout.alignment: Qt.AlignLeft
    color: input.focus ? "lightblue" : "lightgray"
    TextInput {
        id: input
        topPadding: 4
        leftPadding: 4
        anchors.fill: parent
        Layout.alignment: Qt.AlignBottom
        color: "black"
    }
}
