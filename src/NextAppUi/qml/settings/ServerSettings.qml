import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

ScrollView {
    anchors.fill: parent

    Settings {
        id: settings
        property string serverAddress : NaComm.defaultServerAddress        
    }

    function commit() {
        settings.serverAddress = address.text
        settings.setValue("pagination/page_size", pageSize.text)
        settings.setValue("server/auto_login", autoLogin.checked)
        NaComm.reloadSettings()
    }

    GridLayout {
        width: parent.width
        rowSpacing: 4
        columns: 2
        //flow: GridLayout.TopToBottom

        Label {
            id: label
            text: qsTr("Server")
        }

        DlgInputField {
            Layout.fillWidth: true
            id: address
            text: settings.serverAddress
        }

        Label { text: qsTr("Page Size")}
        DlgInputField {
            id: pageSize
            text: settings.value("pagination/page_size", 500)
            //placeholderText: qsTr("20 - 200");
            Layout.preferredWidth: 80
        }

        Item {}

        CheckBox {
            id: autoLogin
            checked: settings.value("server/auto_login", true)
            text: qsTr("Auto Login")
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
