import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Dialog {
    id: root
    title: qsTr("Edit Categories")
    x: NaCore.isMobile ? 0 : (ApplicationWindow.window.width - width) / 3
    y: NaCore.isMobile ? 0 : (ApplicationWindow.window.height - height) / 3
    width: Math.min(600, ApplicationWindow.window.width - 10)
    height: Math.min(800, ApplicationWindow.window.height - 10)
    standardButtons: Dialog.Ok
    property NextappPB.actionCategory actionCategory: NaAcModel.get(-1)

    function enableSave(enable) {
        if (enable) {
            standardButtons = Dialog.Save | Dialog.Cancel
        } else {
            standardButtons = Dialog.Cancel
        }
    }

    function validate() {
        if (nameField.text === "") {
            return false
        }
        return true
    }

    onVisibleChanged: {
        if (visible) {
            nameField.text = actionCategory.name
            descrtiption.text = actionCategory.descr
            color.colorName = actionCategory.color
            //color.background.color = actionCategory.color
        }
    }

    background: Rectangle {
        color: "white"
    }

    GridLayout {
        anchors.fill: parent
        rowSpacing: 4
        columns: 2
        enabled: NaAcModel.valid

        Label {
            text: qsTr("Category Name")
        }

        DlgInputField {
            id: nameField
            Layout.fillWidth: true
            onChanged: {
                enableSave(validate())
                // TODO: Prevent duplicate names
            }
        }

        Label {
            text: qsTr("Category Description")
        }

        DlgInputField {
            id: descrtiption
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Category Color")
        }

        Button {
            id: color
            property string colorName: "gray"

            text: colorName

            background: Rectangle {
                color: color.colorName
            }

            onClicked: {
                colorPicker.open()
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    ColorPicker {
        id: colorPicker

        onColorSelectionChanged: (choosenColor) => {
            actionCategory.color = choosenColor
            color.colorName = choosenColor
        }
    }

    onAccepted: {
        if (validate()) {
            actionCategory.name = nameField.text
            actionCategory.descr = descrtiption.text

            if (actionCategory.id_proto === "") {
                NaAcModel.createCategory(actionCategory)
            } else {
                NaAcModel.updateCategory(actionCategory)
            }
        }
    }

    onClosed: {
        if (colorPicker.visible) {
            colorPicker.close()
        }
    }
}
