import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

Dialog {
    id: resetDialog
    title: qsTr("Factory Reset")
    standardButtons: Dialog.Yes | Dialog.No
    width: 400
    height: 600

    contentItem: ColumnLayout {
        spacing: 10

        // ScrollView to allow scrolling long messages
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: errorMessage
                text: qsTr("Are you sure you want to reset NextApp?\nThis will open the Signup wizard next time you start NextApp. Your current local configuration and settings will be lost. You will have to re-add this device using an One Time Password (OTP) from another device, or sign up for a new account.\nNote that resetting the app on this device will not delete your account or your data on the server.")
                wrapMode: TextArea.Wrap
                readOnly: true
                selectByMouse: true  // Allow selecting text for copying
                background: Rectangle {
                    color: "transparent"  // Make it blend with the dialog
                }
            }
        }
    }


    onAccepted: {
        NaCore.factoryReset()
    }
}
