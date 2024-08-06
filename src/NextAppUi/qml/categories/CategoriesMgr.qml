import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Dialog {
    id: root
    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(800, NaCore.height - 100, Screen.height)
    visible: true
    standardButtons: Dialog.Close
    property font headerFont: Qt.font({pixelSize: 18, bold: true})

    header: RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        Label {
            font: root.headerFont
            text: qsTr("Edit Categories")
            Layout.alignment: Qt.AlignCenter
        }
    }

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
            clip: true
            spacing: 6

            ItemSelectionModel {
                id: selectionModel
                model: listCtl.model
            }

            ScrollBar.vertical: ScrollBar {
                id: vScrollBar
                parent: listCtl
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: MaterialDesignStyling.scrollBarWidth
                policy: ScrollBar.AlwaysOn
            }


            delegate: Item {
                id: item
                width: list.width - MaterialDesignStyling.scrollBarWidth - 2;
                height: layout.implicitHeight
                required property int index
                required property string name
                required property string descr
                required property string color

                property bool selected: selectionModel.hasSelection ? selectionModel.isSelected(listCtl.model.index(index, 0)) : false
                property string bgColor: selected ? MaterialDesignStyling.onPrimaryContainer : MaterialDesignStyling.onPrimary
                property string fgColor: selected ? MaterialDesignStyling.primaryContainer : index % 2 ? MaterialDesignStyling.primary : MaterialDesignStyling.primaryFixed

                Rectangle {
                    anchors.fill: parent
                    width: parent.width
                    color: bgColor
                    radius: 5

                    ColumnLayout {
                        id: layout
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        Text {
                            text: item.name
                            font: root.headerFont
                            color: item.fgColor
                        }

                        Text {
                            text: item.descr
                            color: item.fgColor
                            visible: item.descr.length > 0
                        }

                        RowLayout {
                            Text {
                                text: qsTr("Color:")
                                color: item.fgColor
                            }
                            Item {
                                Layout.preferredWidth: 10
                            }
                            Rectangle {
                                Layout.preferredWidth: 100
                                Layout.preferredHeight: 20
                                radius: 5
                                color: item.color
                                Text {
                                    text: item.color
                                    color: MaterialDesignStyling.onPrimary
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }

                    TapHandler {

                        onTapped: {
                            selectionModel.select(listCtl.model.index(index, 0), ItemSelectionModel.Select | ItemSelectionModel.Current)
                            // console.log("selected")
                        }

                        onLongPressed: {
                            editDlg.actionCategory = listCtl.model.get(index)
                            selectionModel.clear();
                            editDlg.open()
                        }
                    }
                }
            }
        }

        GridLayout {
            RoundButton {
                text: qsTr("Add")

                onClicked: {
                    editDlg.actionCategory = listCtl.model.get(-1)
                    editDlg.open()
                }
            }

            RoundButton {
                enabled: selectionModel.hasSelection
                text: qsTr("Edit")
                onClicked: {
                    editDlg.actionCategory = listCtl.model.get(selectionModel.selectedIndexes[0].row)
                    selectionModel.clear();
                    editDlg.open()
                }
            }

            RoundButton {
                enabled: selectionModel.hasSelection
                text: qsTr("Remove")
                onClicked: {
                    confirmDelete.open()
                }

            }
        }
    }


    EditCategoryDlg {
        id: editDlg
    }

    MessageDialog {
        id: confirmDelete

        title: qsTr("Do you really want to delete the Category?")
        text: qsTr("Note that if the category is referenced by other objects, the delete command will fail.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
            listCtl.model.deleteSelection(selectionModel.selectedIndexes)
            selectionModel.clear();
            confirmDelete.close()
        }

        onRejected: {
            selectionModel.clear();
            confirmDelete.close()
        }
    }
}
