// SyncPopup.qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Dialog {
    id: syncDialog
    modal: true
    title: "Sync in Progress"

    contentItem: Text {
        text: "Synchronizing with the server, please wait..."
        anchors.centerIn: parent
    }

    onAccepted: {
        // Handle dialog accepted event if needed
    }
}
