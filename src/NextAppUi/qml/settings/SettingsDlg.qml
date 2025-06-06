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
    width: Math.min(parent.width, 800)
    height: Math.min(parent.height - 10, 600)

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Settings")

    ColumnLayout {
        anchors.margins: 20
        anchors.fill: parent
        TabBar {
            id: tab
            width: parent.width

            // TabButton {
            //     text: qsTr("Server")
            //     width: implicitWidth
            // }

            TabButton {
                text: qsTr("Global")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Appearance")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Notifications")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Advanced")
                width: implicitWidth
            }
        }

        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: tab.currentIndex
            // Item {
            //     id: serverTab
            //     ServerSettings {id: server}
            // }
            Item {
                id: globalTab
                GlobalSettings {id: global}
            }
            Item {
                id: preferencesTab
                PrefSettings {id: preferences}
            }
            Item {
                id: notificationsTab
                NotificationSettings {id: notifications}
            }
            Item {
                id: advancedTab
                AdvancedSettings {id: advanced}
            }
        }
    }

    onAccepted: {
        //server.commit()
        global.commit()
        preferences.commit()
        advanced.commit()
        notifications.commit()
        close()
    }

    onRejected: {
        close()
    }
}

