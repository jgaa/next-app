import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb
Item {
    anchors.fill: parent

    Settings {
        id: settings
        property string serverAddress : NaComm.defaultServerAddress        
    }

    function commit() {
        settings.serverAddress = address.text
        settings.setValue("pagination/page_size", pageSize.text)
        NaComm.reloadSettings()
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2
        //flow: GridLayout.TopToBottom

        Label { text: qsTr("Server")}

        TextField {
            Layout.fillWidth: true
            id: address
            text: settings.serverAddress
            placeholderText: qsTr("https://nextapp.lastviking.eu");
            width: 150
        }

        Label { text: qsTr("Page Size")}
        TextField {
            Layout.fillWidth: true
            id: pageSize
            text: settings.value("pagination/page_size", 100)
            placeholderText: qsTr("20 - 200");
            width: 150
        }
    }
}
