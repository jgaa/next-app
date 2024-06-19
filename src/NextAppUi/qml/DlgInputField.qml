import QtQuick
import QtQuick.Layouts
import QtQuick.Controls


Rectangle {
    property alias text: input.text
    property alias inputMask: input.inputMask
    property alias validator: input.validator
    Layout.preferredHeight: input.font.pixelSize + 12
    color: input.focus ? "lightblue" : "lightgray"
    signal changed()
    TextInput {
        id: input
        topPadding: 4
        leftPadding: 4
        anchors.fill: parent
        //Layout.alignment: Qt.AlignBottom
        color: "black"

        onTextChanged: {
            parent.changed()
        }
    }
}
