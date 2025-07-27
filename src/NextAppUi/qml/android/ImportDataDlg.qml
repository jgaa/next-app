import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    title: qsTr("Import Data")
    standardButtons: Dialog.Close
    width: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.width, 450) : 100
    height: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.height - 10, 650) : 100
    property string fileUrl: ""

    Connections {
        target: NaComm
        function onResynching() {
            //root.close()
            fadeOut.start()
        }
    }

    SequentialAnimation {
        id: fadeOut
        NumberAnimation {
            target: root;
            property: "opacity";
            to: 0.0;
            duration: 300
        }
        ScriptAction { script: root.close() }
    }

    onClosed: {
        root.opacity = 1.0
    }

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        spacing: 20

        Label {
            text: qsTr("Note: Importing data will replace all existing data in the application on all devices connected to this user account.\nThis feature is intended to help you migrate your data when creating a new user account on a different server.\n\nFor example, you can use it if you initially signed up on a public server and have since set up your own server.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Import / Restore Data")

            onClicked: {
                confirmImport.open()
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }


    BusyIndicator {
        id: busyIndicator
        running: ImportExportModel.working
        visible: ImportExportModel.working
        anchors.horizontalCenter: contentLayout.horizontalCenter
        anchors.verticalCenter: contentLayout.verticalCenter
    }

    MessageDialog {
        id: confirmImport

        title: qsTr("Danger Zone")
        text: qsTr("All your existing data will be replaced with the data from this file.\nThis action cannot be undone.\n\nAre you sure you want to continue?")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           ImportExportModel.importData(root.fileUrl);
           confirmImport.close()
        }

        onRejected: {
            confirmImport.close()
        }
    }

}
