import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models


ColumnLayout  {
    id: root
    anchors.fill: parent
    spacing: 20
    signal nextClicked()


    TextArea {
        text: "You are now ready to start using Nextapp!"
        font.pixelSize: 20
        textFormat: Text.RichText
    }

    Button {
        id: nextBtn
        text: qsTr("Start using Nextapp!")
        onClicked: {
            nextClicked()
        }
    }

    Item {
        Layout.preferredHeight: 20
    }
}

