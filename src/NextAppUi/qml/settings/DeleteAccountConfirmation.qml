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
    title: qsTr("Delete Account")
    standardButtons: Dialog.Yes | Dialog.No
    width: 400
    height: 300

    contentItem: ColumnLayout {
        spacing: 10

        // ScrollView to allow scrolling long messages
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: errorMessage
                text: qsTr("Are you sure you want to delete your account?\nThis will open the sign-up wizard next time you start NextApp. Your current data will be deleted on this device and on the server.\nIf you start the app on another device, you will be unable to log on, but you will be asked if you want to delete the data on that device.\nThis action cannot be undone.")
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
        // settings.setValue("onboarding", false);
        // Qt.callLater(Qt.quit)
    }
}
