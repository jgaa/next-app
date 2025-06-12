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
            text: qsTr("Export Data in NextApp format")

            onClicked: {
                console.log("Export Data button clicked");
                saveDialog.open()
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
        title: qsTr("Save Data to File")
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
}
