import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    property var model: OtpModel {}
    x: NaCore.isMobile ? 0 : (parent.width - width) / 2
    y: NaCore.isMobile ? 0 : (parent.height - height) / 2
    width: Math.min(NaCore.width, 400)
    height: Math.min(NaCore.height, 500)

    standardButtons: Dialog.Close
    title: qsTr("One Time Password")

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        Item {}

        Button {
            text: qsTr("Request a new OTP")
            onClicked: {
                model.requestOtpForNewDevice()
            }
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            text: qsTr("Error")
            visible: error.visible
        }

        TextField{
            id: error
            readOnly: true
            text: model.error
            visible: model.error !== ""
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            text: qsTr("One Time Password")
        }

        TextInput{
            id: otp
            readOnly: true
            text: model.otp
            inputMask: "9999 9999"
        }

        Label {
            Layout.alignment: Qt.AlignLeft
            text: qsTr("The email address to use")
        }

        TextField{
            id: email
            readOnly: true
            text: model.email
        }
    }
}
