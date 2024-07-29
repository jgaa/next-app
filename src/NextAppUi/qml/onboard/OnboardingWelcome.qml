import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

ColumnLayout  {
    anchors.fill: parent
    spacing: 20
    signal nextClicked()

    TextArea {
        Layout.fillWidth: true
        Layout.fillHeight: true
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.RichText

        text: qsTr("<h1>Welcome to Nextapp!</h1>"
                   + "<p>This application stores its data in \"The Cloud\" and need "
                   + "to be connected to the internet and to an external server to work.</p>"
                   + "<p>Before you can start using it, I must know what backend server you sill be using. "
                   + "You can use the public one, or you may deploy your own server for maximum privacy.</p>"
                   + "")

        wrapMode: Text.WordWrap
        readOnly: true
        antialiasing: true
        color: MaterialDesignStyling.onSurface
        background: Rectangle {
            color: "transparent"
        }
    }

    Item {Layout.fillHeight: true}

    RowLayout {
        Layout.fillWidth: true
        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("Next")
            onClicked: nextClicked()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    Item {
        Layout.preferredHeight: 20
    }
}

