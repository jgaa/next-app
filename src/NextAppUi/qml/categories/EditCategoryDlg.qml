import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB

Dialog {
    id: root
    title: qsTr("Edit Categories")
    width: 600
    height: 800
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

    onActionCategoryChanged: {
        nameField.text = actionCategory.name
        descriptionField.text = actionCategory.descr
        color.text = actionCategory.color
        color.background.color = actionCategory.color
    }

    background: Rectangle {
        color: "white"
    }

    GridLayout {
        Layout.alignment: Qt.AlignLeft
        Layout.fillHeight: true
        Layout.fillWidth: true
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
            id: descriptionField
            text: actionCategory.description
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Category Color")
        }

        Button {
            id: color
            property string colorName: "gray"

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

        onColorSelectionChanged: (color) => {
            actionCategory.color = color
            ////color.background.color = colorPicker.color
            color.colorName = color
        }
    }

    onAccepted: {
        if (validate()) {
            actionCategory.name = nameField.text
            actionCategory.descr = descriptionField.text

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
