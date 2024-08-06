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
    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(800, NaCore.height - 100, Screen.height)
    standardButtons: Dialog.Ok
    property NextappPB.actionCategory actionCategory: ActionCategoriesModel.get(-1)

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
        enabled: ActionCategoriesModel.valid

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
                ActionCategoriesModel.createCategory(actionCategory)
            } else {
                ActionCategoriesModel.updateCategory(actionCategory)
            }
        }
    }

    onClosed: {
        if (colorPicker.visible) {
            colorPicker.close()
        }
    }
}
