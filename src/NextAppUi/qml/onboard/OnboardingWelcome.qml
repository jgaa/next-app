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
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.RichText
        font.pointSize: 12

        text: qsTr(
                  "<h1>Welcome to NextApp!</h1>\n"
                + "<h3>Your Personal Organizer</h3>\n"
                + "<p>To continue, make sure you’re online.</p>\n"
                + "<p>Let’s get you up and running!</p>"
              )


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

