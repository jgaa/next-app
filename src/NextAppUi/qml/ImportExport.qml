import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    title: "Import / Export Data"
    standardButtons: Dialog.Close
    x: NaCore.isMobile ? 0 : (ApplicationWindow.window.width - width) / 3
    y: NaCore.isMobile ? 0 : (ApplicationWindow.window.height - height) / 3
    width: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.width, 450) : 100
    height: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.height - 10, 650) : 100

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        spacing: 20

        Label {
            text: qsTr("For backups or when migrating to another server, export your data to a .nextapp file, which you can then import on the target server. If you want to inspect your data or migrate it to a different application, export it with a .json extension; it will be saved in standard JSON format.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Export Data")

            onClicked: {
                saveDialog.open()
            }
        }

        Label {
            text: qsTr("Note: Importing data will replace all existing data in the application on all devices connected to this user account.\nThis feature is intended to help you migrate your data when creating a new user account on a different server.\n\nFor example, you can use it if you initially signed up on a public server and have since set up your own server.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Import / Restore Data")

            onClicked: {
                restoreDialog.open()
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

    FileDialog {
        id: saveDialog
        title: qsTr("Backup / Save your data to file")
        currentFolder: ImportExportModel.dataPath
        nameFilters: [qsTr("NextApp files (*.nextapp)"), qsTr("Json files (*.json)")]
        defaultSuffix: "nextapp"
        fileMode: FileDialog.SaveFile

        onAccepted: {
            if (selectedFile !== "") {
                ImportExportModel.exportData(selectedFile);
            }
        }
    }

    MessageDialog {
        id: confirmImport
        property url fileUrl: ""

        title: qsTr("Danger Zone")
        text: qsTr("All your existing data will be replaced with the data from this file.\nThis action cannot be undone.\n\nAre you sure you want to continue?")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           ImportExportModel.importData(fileUrl);
           confirmImport.close()
        }

        onRejected: {
            confirmImport.close()
        }
    }

    FileDialog {
        id: restoreDialog
        title: qsTr("Restore backup / import data")
        currentFolder: ImportExportModel.dataPath
        nameFilters: [qsTr("NextApp files (*.nextapp)")]
        defaultSuffix: "nextapp"
        fileMode: FileDialog.OpenFile

        onAccepted: {
            if (selectedFile !== "") {
                confirmImport.fileUrl = selectedFile;
                confirmImport.open();
            }
        }
    }
}
