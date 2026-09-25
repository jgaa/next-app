import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb
import "../common.js" as Common

ScrollView {
    id: root
    anchors.fill: parent
    Settings {
        id: settings
    }

    function commit() {
        settings.setValue("UI/theme", uiTheme.currentValue)
        MaterialDesignStyling.setTheme(uiTheme.currentValue)
        settings.setValue("UI/style", uiStyle.currentIndex.toString())
        settings.setValue("UI/mobile/persistDualViewSplit", persistDualViewSplit.checked)
        //settings.setValue("UI/scale", uiScale.currentIndex.toString())
        settings.sync()
    }

    GridLayout {
        width: parent.width
        rowSpacing: 4
        columns: 2

        Label { text: qsTr("Ui Theme")}
        ComboBox {
            id: uiTheme
            textRole: "label"
            valueRole: "name"
            model: MaterialDesignStyling.availableThemes().map(function(name) {
                return { name: name, label: (name.charAt(0).toUpperCase() + name.slice(1)).replace(/([a-z])([A-Z])/g, "$1 $2") }
            })
            Component.onCompleted: {
                const index = indexOfValue(settings.value("UI/theme", "light"))
                currentIndex = index >= 0 ? index : 0
            }
        }

        Item {}
        Button {
            text: qsTr("Preview Theme Schemes")
            Layout.alignment: Qt.AlignLeft
            onClicked: Common.openDialog("settings/ThemeSchemePreviewDialog.qml", parent, {})
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

        Label { text: qsTr("Remember mobile split size") }
        Switch {
            id: persistDualViewSplit
            visible: NaCore.isMobile
            checked: settings.value("UI/mobile/persistDualViewSplit", false)
            Layout.alignment: Qt.AlignLeft
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
