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
        property string uiTheme : "light"
    }

    function commit() {
        var name = uiTheme.currentText
        settings.setValue("UI/theme", name)
        MaterialDesignStyling.setTheme(name)
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Ui Theme")}
        ComboBox {
            id: uiTheme
            currentIndex: settings.value("UI/theme") === "light" ? 0 : 1
            model: ["light", "dark"]
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
