import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NextAppUi         // (whatever module exposes your UI types)
import Nextapp.Models 1.0 // (where NextAppCore is registered as NaCore)
import nextapp.pb as NextappPb

Dialog {
    id: resetDialog
    title: qsTr("Delete Account")
    standardButtons: Dialog.Yes | Dialog.No
    width: 400
    height: 600

    contentItem: ColumnLayout {
        spacing: 10

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: errorMessage
                text: qsTr("Are you sure you want to delete your account?\n"
                            + "This will open the sign-up wizard next time you start NextApp. "
                            + "Your current data will be deleted on this device and on the server.\n"
                            + "If you start the app on another device, you will be unable to log on, "
                            + "but you will be asked if you want to delete the data on that device.\n"
                            + "This action cannot be undone.")
                wrapMode: TextArea.Wrap
                readOnly: true
                selectByMouse: true
                background: Rectangle { color: "transparent" }
            }
        }
    }

    onAccepted: {
        deletingPopup.errorOccurred = false
        deletingPopup.visible = true
        NaCore.deleteAccount()
    }


    Popup {
        id: deletingPopup

        // Make it modal so it sits on top
        modal: true

        // Don’t let clicks/Esc close it automatically
        closePolicy: Popup.NoAutoClose

        // Fixed size (you can adjust to taste)
        width: 350
        height: 220

        // This Rectangle *is* the interior of the popup.
        // All children (Text, BusyIndicator, Button) must go inside it.
        contentItem: Rectangle {
            anchors.fill: parent
            color: "white"
            radius: 6
            border.color: "#888"
            border.width: 1

            // Use margins so text/buttons aren’t glued to the edge
            anchors.margins: 16

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 12

                // BusyIndicator: only visible when there is no error
                BusyIndicator {
                    id: busyIndicator
                    running: !deletingPopup.errorOccurred
                    visible: !deletingPopup.errorOccurred
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                // This is where the “Deleting account…” or the error message goes.
                Text {
                    id: statusText
                    text: deletingPopup.errorOccurred
                          ? deletingPopup.errorMessage
                          : qsTr("Deleting account…")
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    font.pointSize: 12
                    // Make sure it doesn’t overflow horizontally:
                    width: parent.width - 32
                }

                // Only show “Close” if an error occurred
                Button {
                    id: closeButton
                    text: qsTr("Close")
                    visible: deletingPopup.errorOccurred
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        deletingPopup.close()
                    }
                }
            }
        }

        // Expose two properties so we can flip between “busy” and “error”
        property bool errorOccurred: false
        property string errorMessage: ""

        onClosed: {
            // When this popup closes (either from success or after an error),
            // make sure to close the underlying confirmation dialog as well.
            if (resetDialog.visible) {
                resetDialog.close()
            }
        }
    }


    Connections {
        target: NaCore

        function onAccountDeleted() {
            // Deletion succeeded → just close the popup.
            deletingPopup.close()
        }

        function onAccountDeletionFailed(message) {
            // Deletion failed → show the error text, stop the busy indicator,
            // and display the “Close” button so the user can dismiss.
            deletingPopup.errorOccurred = true
            deletingPopup.errorMessage = message
            // At this point, busyIndicator (running: !errorOccurred) will stop
            // and the “Close” button will become visible.
        }
    }
}
