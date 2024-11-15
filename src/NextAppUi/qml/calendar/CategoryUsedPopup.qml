import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

Popup {
    id: root
    modal: true
    property alias model: categoriesUsed.model
    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(300, NaCore.width, Screen.width)
    height: Math.min(500, NaCore.height, Screen.height)

    Rectangle {
        color: MaterialDesignStyling.surfaceBright
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent

        Text {
            id: hiddenText
            visible: false
        }

        Text {
            text: qsTr("Time per Category")
            font.pointSize: hiddenText.font.pointSize * 1.5
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
            Layout.margins: 16
            horizontalAlignment: Text.AlignHCenter
            color: MaterialDesignStyling.onPrimaryContainer
        }

        CategoriesUsed {
            id: categoriesUsed
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        StyledButton {
            text: qsTr("Close")
            Layout.fillWidth: true
            onClicked: root.close()
        }
    }
}
