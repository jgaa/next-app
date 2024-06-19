import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    x: NaCore.isMobile ? 0 : (parent.width - width) / 3
    y: NaCore.isMobile ? 0 : (parent.height - height) / 3
    width: NaCore.isMobile ? Screen.width : 800
    height: NaCore.isMobile ? Screen.height - 100 : 600

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

            TabButton {
                text: qsTr("Preferences")
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
            Item {
                id: preferencesTab
                PrefSettings {id: preferences}
            }
        }
    }

    onAccepted: {
        server.commit()
        global.commit()
        preferences.commit()
        close()
    }

    onRejected: {
        close()
    }
}

