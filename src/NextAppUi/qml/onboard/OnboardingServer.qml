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
    signal nextNewSubscrClicked()
    signal nextAddDeviceClicked()
    signal backClicked()

    Settings {
        id: settings
        property string serverAddress : NaComm.defaultServerAddress
    }

    // Connections element to listen to NaComm.signupStatus changes
    Connections {
        target: NaComm
        function onSignupStatusChanged() {
            if (NaComm.signupStatus === NaComm.SIGNUP_HAVE_INFO) {
                if (modeCtl.currentIndex === 0) {
                    root.nextNewSubscrClicked();
                } else {
                    root.nextAddDeviceClicked();
                }
            }
        }
    }

    TextArea {
        Layout.fillWidth: true
        Layout.fillHeight: true
        textFormat: Text.RichText
        font.pointSize: 12

        text: qsTr("<h2>Choose a cloud server to get started:</h2></p>\n"
                   + "<ul>\n"
                   + "  <li><strong>Public Server</strong> — Quick setup. Free trial. Ready to go. Just press <strong>Next</strong>.</li>\n"
                   + "  <li><strong>Private Server</strong> — <a href href=\"https://next-app.org/private_backend.html\">Deploy your own</a> for even better privacy or legal compliance.</li>\n"
                   + "</ul>\n"
                   + "<p>When you have choosen a server, you will be presented with the conditions "
                   + "of use for that server. You can then choose to accept these conditions to continue.</p>"
                   + "")

        wrapMode: Text.WordWrap
        readOnly: true
        antialiasing: true
        color: MaterialDesignStyling.onSurface
        background: Rectangle {
            color: "transparent"
        }

        HoverHandler {
            enabled: parent.hoveredLink.length > 0
            cursorShape: Qt.PointingHandCursor
        }

        onLinkActivated: function(link) {
            Qt.openUrlExternally(link)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 20
    }

    RowLayout {
        Layout.leftMargin: 25
        Layout.rightMargin: 25
        Layout.fillWidth: true
        spacing: 10

        Label {
            id: label
            text: qsTr("Server")
            color: MaterialDesignStyling.onSurface
        }

        DlgInputField {
            Layout.fillWidth: true
            //Layout.preferredWidth: (root.width / 3) * 2
            id: address
            text: settings.serverAddress
        }
    }

    RowLayout {
        Item {Layout.preferredWidth: 20}
        ComboBox {
            id: modeCtl
            Layout.fillWidth: true
            currentIndex: 0
            model: ListModel {
                ListElement { name: "New subscription" }
                ListElement { name: "Add device to existing user" }
            }
            textRole: "name"
        }
        Item {Layout.preferredWidth: 20}
    }

    Item {Layout.preferredHeight: 20}


    RowLayout {
        spacing: 20

        Layout.fillWidth: true
        Item {
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Back")
            onClicked: backClicked()
        }

        Button {
            text: qsTr("Connect")
            visible: !nextBtn.visible
            onClicked: {
                settings.serverAddress = address.text
                NaComm.setSignupServerAddress(settings.serverAddress)
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
