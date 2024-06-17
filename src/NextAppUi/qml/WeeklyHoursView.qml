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
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5
    color: MaterialDesignStyling.surface

    property int cellwidth: 70
    property string prevLabel: ""

    ColumnLayout {
        anchors.fill: parent

        ToolBar {
            Layout.fillWidth: true

            background: Rectangle {
                color: MaterialDesignStyling.surfaceContainer
            }

            RowLayout {
                spacing: 6
                StyledComboBox {
                    id: selectionCtl
                    // Don't work!
                    //currentIndex: tableView.model.weekSelection
                    model: ListModel {
                        ListElement { text: qsTr("This week") }
                        ListElement { text: qsTr("Last week") }
                        ListElement { text: qsTr("Select week") }
                    }

                    onCurrentIndexChanged: () => {
                        prevLabel = displayText
                        tableView.model.weekSelection = currentIndex

                        if (currentIndex === 2) {
                            datePicker.open()
                            currentIndex = -1
                        } else {
                            displayText = textAt(currentIndex)
                        }
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

            StyledHeaderView {
                id: horizontalHeader
                anchors.left: parent.left
                anchors.top: parent.top
                syncView: tableView
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
                        required property bool selected
                        required property bool summary // Last line

                        implicitHeight: summary ? 32 : 24
                        implicitWidth: root.cellwidth

                        color: summary
                           ? MaterialDesignStyling.tertiaryContainer
                           : column % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer
                        border {
                            color: MaterialDesignStyling.outline
                            width: 1
                        }

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: column === 0 ?  Text.AlignRight : Text.AlignHCenter
                            color: summary ? MaterialDesignStyling.onTertiaryContainer : MaterialDesignStyling.onSurface
                            text: display
                            font.bold: summary
                            padding: 5
                        }
                    }
                }
            }
        }
    }

    DatePicker {
        id: datePicker
        mode: NextappPB.ActionDueKind.WEEK
        modal: true

        onSelectedWeekClosed: (date, accepted, week) => {
            console.log("Selected week: " + date + ", accepted = " + accepted + ", week: " + week)
            if (accepted) {
                tableView.model.startTime = date.getTime() / 1000
                selectionCtl.displayText = qsTr("Week #%1/%2").arg(week).arg(datePicker.currentYear)
            } else {
                selectionCtl.displayText = prevLabel
            }

            datePicker.close()
        }
    }
}
