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
        Layout.margins: 20
        Layout.fillWidth: true
        Layout.fillHeight: true
        text: qsTr("<h2>Congratulations!</h2>"
                   + "<p>You are now ready to start using Nextapp!</p>"
                   + "<p>You can find free documentations, FAQ and instrcution videos on "
                   + "<a href='https://www.next-app.org'>www.next-app.org</a></p>")
        font.pixelSize: 20
        textFormat: Text.RichText
        wrapMode: Text.WordWrap
        readOnly: true
        antialiasing: true
        background : Rectangle {
            color: "white"
        }

        onLinkActivated: function(link) {
            Qt.openUrlExternally(link)
        }
    }

    RowLayout {
        spacing: 20
        Layout.fillWidth: true

        Item {
            Layout.fillWidth: true
        }

        Button {
            id: nextBtn
            text: qsTr("Start using Nextapp!")
            onClicked: {
                nextClicked()
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    Item {
        Layout.preferredHeight: 20
    }
}

