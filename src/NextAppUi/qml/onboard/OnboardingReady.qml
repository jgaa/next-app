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
    property bool newUser: false
    signal nextClicked()


    TextArea {
        Layout.margins: 20
        Layout.fillWidth: true
        Layout.fillHeight: true
        text: qsTr("<h2>Congratulations!</h2>"
                   + "<p>You are now ready to start using Nextapp!</p>"
                   + "<p>You can find free documentations, FAQ and instrcution videos on "
                   + "<a href='https://next-app.org'>next-app.org</a></p>")
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

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        Layout.margins: 20
        visible: root.newUser

        Label {
            text: qsTr("Create lists from template")
            color: "white"
            font.bold: true
        }

        ComboBox {
            model: UseCaseTemplates.getTemplateNames()
            id: useCaseCombo
            Layout.fillWidth: true
            //Layout.preferredHeight: 40
            background: Rectangle {
                color: "white"
            }
        }

        Text {
            id: useCaseDescription
            Layout.fillWidth: true
            text: UseCaseTemplates.getDescription(useCaseCombo.currentIndex)
            wrapMode: Text.WordWrap
            antialiasing: true
            color: "white"
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
            text: useCaseCombo.currentIndex > 0 ? qsTr("Create lists from template and start using Nextapp!") : qsTr("Start using Nextapp!")
            onClicked: {
                UseCaseTemplates.createFromTemplate(useCaseCombo.currentIndex)
                NaCore.bootstrapDevice(root.newUser)
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

