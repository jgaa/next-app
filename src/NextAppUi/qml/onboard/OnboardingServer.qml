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

    TextArea {
        Layout.fillWidth: true
        Layout.fillHeight: true
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.RichText

        text: qsTr("<h1>Select server to use!</h1>"
                   + "<p>The default server is a good choise if you want to start using the "
                   + "application right away.</p>"
                   + "<p>A server is simply an accompanying application that runs on a computer "
                   + "and stores and syncronize the data used by the application. </p>"
                   + "<p>The default server (below) is running in the \"cloud\" which is a fancy way "
                   + "of saying that it is running on somebody elses computer.</p>"
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
    }

    ScrollView {
        Layout.leftMargin: 20
        Layout.rightMargin: 20
        Layout.preferredHeight: 50
        Layout.fillWidth: true

        Text {
            text: NaComm.messages
            wrapMode: Text.Wrap
            color: MaterialDesignStyling.onSurface
        }
    }

    Label {
        id: label
        text: qsTr("Server")
        color: MaterialDesignStyling.onSurface
    }

    RowLayout {
        Layout.fillWidth: true

        Item {
            Layout.fillWidth: true
        }
        DlgInputField {
            Layout.preferredWidth: (root.width / 3) * 2
            id: address
            text: settings.serverAddress
        }
        Item {
            Layout.fillWidth: true
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

        Button {
            id: nextBtn
            text: qsTr("Next")
            visible: NaComm.signupStatus === NaComm.SIGNUP_HAVE_INFO
            onClicked: {
                if (modeCtl.currentIndex === 0) {
                    nextNewSubscrClicked()
                } else {
                    nextAddDeviceClicked()
                }
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
