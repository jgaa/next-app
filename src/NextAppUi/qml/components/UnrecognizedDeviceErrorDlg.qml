import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import Nextapp.Models 1.0 // (where NextAppCore is registered as NaCore)
import nextapp.pb as NextappPb

// We create this component from the C++ code on a fatal error condition.
// It's non-trivial to open a QML Dialog direcctly from C++, as the underlaying
// type, QQuickPopup, is not exposed to the C++ QT API.
// So we create an Item, which is exposed, and let it add the Dialog.
Item {
    id: root
    anchors.fill: parent
    visible: true

    component ErrorDialog : Dialog {
        id: errorDialog
        title: qsTr("Unrecoverable Error")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        // Put it in center of thye parent
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2

        width: 400
        height: 300

        Settings {
            id: settings
        }

        contentItem: ColumnLayout {
            spacing: 10

            // ScrollView to allow scrolling long messages
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                TextArea {
                    Layout.fillWidth: true
                    id: errorMessage
                    text: qsTr("The server does not recognize this device. You should re-run the signup process, select 'Add Device' and then use a One Time Password (OTP) from another device to authorize it.\nDo you want to do this now?\n\nAlternatively, if you deleted your account, you can press \"Delete Data\" to remove the local copy of your NextApp data.")
                    wrapMode: TextArea.Wrap
                    readOnly: true  // Prevent accidental edits
                    selectByMouse: true  // Allow selecting text for copying
                    background: Rectangle {
                        color: "transparent"  // Make it blend with the dialog
                    }
                }
            }

            Button {
                text: qsTr("Delete Data")
                onClicked: {
                    NaCore.deleteLocalData()
                }
            }
        }

        onAccepted: {
            NaCore.deleteLocalData()
        }

        function setErrorMessage(message) {
            errorMessage.text = message;
        }
    }

    ErrorDialog {
        id: errorDialog
        // open when it is created
    }

    // run s when the component is loaded
    Component.onCompleted: {
        errorDialog.open();
    }
}
