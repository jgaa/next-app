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
    }

    function commit() {
        settings.setValue("UI/theme", uiTheme.currentText)
        settings.setValue("UI/style", uiStyle.currentIndex.toString())
        settings.setValue("UI/scale", uiScale.currentIndex.toString())
        settings.sync()
    }

    GridLayout {
        width: parent.width
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Ui Theme")}
        ComboBox {
            id: uiTheme
            currentIndex: settings.value("UI/theme") === "light" ? 0 : 1
            model: ["light", "dark"]
        }

        Label { text: qsTr("Ui Style")}
        ComboBox {
            id: uiStyle
            currentIndex: parseInt(settings.value("UI/style"))
            Layout.fillWidth: true
            model: [qsTr("Default"),
                qsTr("Simple"),
                qsTr("Imagine"),
                qsTr("Desktop"),
                qsTr("Android"),
                // qsTr("macOS"),
                // qsTr("iOS"),
                qsTr("Windows")]
        }

        // Label { text: qsTr("Ui Scale")}
        // ComboBox {
        //     id: uiScale
        //     currentIndex: parseInt(settings.value("UI/scale"))
        //     Layout.fillWidth: true
        //     model: [qsTr("Default"),
        //         qsTr("Very Tiny"),
        //         qsTr("Tiny"),
        //         qsTr("Small"),
        //         qsTr("Normal"),
        //         qsTr("Large"),
        //         qsTr("Larger"),
        //         qsTr("Even larger"),
        //         qsTr("Very large"),
        //         qsTr("Huge"),
        //         qsTr("Very Huge"),]
        // }

        Item {
            Layout.fillHeight: true
        }
    }
}
