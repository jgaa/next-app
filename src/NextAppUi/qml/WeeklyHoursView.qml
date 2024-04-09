import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Rectangle {
    id: root
    Layout.fillHeight: true
    Layout.fillWidth: true
    color: Colors.background

    property int cellwidth: 70

    ColumnLayout {
        anchors.fill: parent

        ToolBar {
            Layout.fillWidth: true

            background: Rectangle {
                color: Colors.background
            }

            RowLayout {
                spacing: 6
                ComboBox {
                    id: selectionCtl
                    model: ListModel {
                        ListElement { text: qsTr("This week") }
                        ListElement { text: qsTr("Last week") }
                        ListElement { text: qsTr("Select week") }
                    }

                    onActivated: (ix) => {
                        // if (prev_selection !== ix) {
                        //     workSessions.model.fetchSome(ix)
                        // }
                        // prev_selection = ix
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            HorizontalHeaderView {
                id: horizontalHeader
                anchors.left: root.left
                anchors.top: root.top
                syncView: tableView
                clip: true
            }

            ScrollView {
                anchors.top: horizontalHeader.bottom
                anchors.left: horizontalHeader.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn

                TableView {
                    id: tableView
                    anchors.fill: parent
                    model: NaCore.createWeeklyWorkReportModel()
                    boundsBehavior: Flickable.StopAtBounds
                    boundsMovement: Flickable.StopAtBounds

                    onVisibleChanged: () => {
                        model.isVisible = root.visible
                    }

                    columnWidthProvider : function (column) {
                        //return 100
                        if (column === 0)
                            return tableView.width - (root.cellwidth * 8);
                        return root.cellwidth;
                    }

                    delegate: Rectangle {
                        id: delegate
                        required property int row
                        required property int column
                        required property var display
                        required property bool summary // Last line

                        color: row % 2 ?  Colors.surface1 : Colors.surface2
                        border {
                            color: Colors.border
                            width: 1
                        }

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: column === 0 ?  Text.AlignRight : Text.AlignHCenter
                            color: summary ? Colors.totals : Colors.text
                            text: display
                            font.bold: summary
                            padding: 5
                        }
                    }
                }
            }
        }
    }
}
