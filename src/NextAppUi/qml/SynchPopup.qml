// SyncPopup.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import NextAppUi
import Nextapp.Models

Rectangle {
    id: root
    color: MaterialDesignStyling.secondaryContainer
    opacity: 0.8
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        id: columnLayout

        Item {
            Layout.fillHeight: true
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Synchronizing with the server, please wait..."
            font.pointSize: 18
            color: MaterialDesignStyling.onSecondaryContainer
        }

        Text {
            Layout.preferredHeight: root.height * 0.6
            Layout.alignment: Qt.AlignHCenter
            font.pointSize: 11
            color: MaterialDesignStyling.onSecondaryContainer
            text: NaComm.messages
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillHeight: true
        }

    }
}
