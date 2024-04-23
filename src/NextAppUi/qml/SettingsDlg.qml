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
                text: qsTr("Global")
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
            Item {
                id: globalTab
                GlobalSettings {id: global}
            }
        }
    }

    onAccepted: {
        server.commit()
        global.commit()
        close()
    }

    onRejected: {
        close()
    }
}

