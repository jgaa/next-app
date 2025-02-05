import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: root
    width: 400
    height: 500
    title: qsTr("Next-app: Your Personal Organizer")
    flags: Qt.Window
    visible: true

    Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 10

            Image {
                source: "/qt/qml/NextAppUi/icons/nextapp.svg"
                sourceSize.width: 100
                sourceSize.height: 100
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("You are already running Next-app.")
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("You cannot start another instance on the same machine.\nPlease use the already running instance.")
                wrapMode: Text.WordWrap
                Layout.alignment: Qt.AlignHCenter
            }

            Item {
                Layout.fillHeight: true
            }

            Button {
                text: qsTr("Close")
                Layout.alignment: Qt.AlignHCenter
                onClicked: Qt.quit()
            }
        }
    }
}
