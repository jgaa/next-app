import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi

Dialog {
    id: root
    x: (parent.width - width) / 3
    y: (parent.height - height) / 3
    width: 800
    height: 600

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Settings")

    ColumnLayout {
        anchors.fill: parent
        TabBar {
            id: tab
            width: parent.width

            TabButton {
                text: qsTr("Server")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Logging")
                width: implicitWidth
            }
        }

        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: tab.currentIndex
            Item {
                id: serverTab
                ServerSettings {id: server}
            }
        }
    }

    onAccepted: {
        server.commit()
        close()
    }

    onRejected: {
        close()
    }
}

