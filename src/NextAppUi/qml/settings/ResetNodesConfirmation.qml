import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NextAppUi
import Nextapp.Models 1.0

Dialog {
    id: resetDialog
    title: qsTr("Start Over")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 460
    modal: true

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: qsTr("This will reset your nodes (lists) to an optional predefined template. You can use this to start over with a clean slate or to switch to a different template. Your account will be reset on the server and all devices will be forced to do a full resync. This cannot be undone.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Template")
            font.bold: true
        }

        ComboBox {
            id: useCaseCombo
            Layout.fillWidth: true
            model: UseCaseTemplates.getTemplateNames()
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: UseCaseTemplates.getDescription(useCaseCombo.currentIndex)
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            color: "#7f1d1d"
            radius: 8
            border.color: "#ef4444"
            border.width: 2
            implicitHeight: warningText.implicitHeight + 24

            Text {
                id: warningText
                anchors.fill: parent
                anchors.margins: 12
                color: "white"
                font.bold: true
                wrapMode: Text.WordWrap
                text: qsTr("Warning: This will delete all your nodes (lists) and permanently remove all related actions, calendar events, and work-sessions on the server for this account. This cannot be undone.")
            }
        }
    }

    onAccepted: {
        UseCaseTemplates.resetFromTemplate(useCaseCombo.currentIndex)
    }
}
