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
    visible: true
    standardButtons: Dialog.Ok

    ColumnLayout {
        enabled: ActionCategoriesModel.valid
        id: list
        anchors.fill: parent

        Label {
            text: qsTr("Categories")
        }

        ListView {
            id: listCtl
            model: ActionCategoriesModel
            Layout.fillHeight: true
            Layout.fillWidth: true

            delegate: Item {
                width: list.width
                height: 40

                Rectangle {
                    width: parent.width
                    height: 40
                    color: "lightgray"
                    border.color: "black"
                    border.width: 1

                    ColumnLayout {
                        id: layout
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        Label {
                            text: model.name
                        }

                        Label {
                            text: model.descr
                        }

                        Label {
                            text: model.color
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            //actionCategory = model
                            //enableSave(false)
                        }
                    }
                }
            }
        }

        RowLayout {
            Button {
                text: qsTr("Add Category")

                onClicked: {
                    editDlg.open()
                }
            }

            Button {
                enabled: list.currentIndex >= 0
                text: qsTr("Remove Category")
            }
        }
    }


    EditCategoryDlg {
        id: editDlg
    }

    onAccepted: {
        //model.save()
    }
}
