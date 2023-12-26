import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: mainTree

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            height: 20

            Rectangle {
                Layout.fillWidth: true
                color: 'lightgray'
            }
        }

        TreeView {
            id: treeView

            Layout.fillHeight: true
            Layout.fillWidth: true
            model: treeModel
            selectionBehavior: TableView.SelectRows

            selectionModel: ItemSelectionModel {
                id: selectModel
                model: treeModel
            }

            delegate: TreeViewDelegate {
                id: treeDelegate


                background: Rectangle {
                    opacity: treeDelegate.selected ? 1 : 0
                    color: "lightblue"
                }

                onClicked: {
                    //selectModel.clearSelection()
                    //selectModel.select(treeView.index(treeDelegate.row, 0),  ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                    //treeDelegate.selected = true
                    var mi = treeDelegate.treeView.index(row, column)
                    console.log("model.index =", mi)
                    console.log("selected =", treeDelegate.selected)
                }
            }
        }
    }
}
