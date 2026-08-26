import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi

Dialog {
    id: root
    parent: Overlay.overlay
    modal: true
    title: qsTr("Unable to connect to NextApp")
    standardButtons: Dialog.Ok
    width: Math.min(parent ? parent.width - 32 : 520, 520)

    property string details: ""

    ColumnLayout {
        width: parent.width
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("NextApp could not connect to and authenticate with the configured server. Please check your network connection and server configuration.")
            wrapMode: Text.WordWrap
        }

        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(implicitHeight, 220)
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WordWrap
            text: qsTr("Technical details: %1\n\nnextappd URL: %2\nsignupd URL: %3\nServer ID: %4\nUser ID: %5")
                .arg(root.details)
                .arg(NaComm.nextappUrl)
                .arg(NaComm.signupUrl)
                .arg(NaComm.serverId)
                .arg(NaComm.userId)
        }
    }
}
