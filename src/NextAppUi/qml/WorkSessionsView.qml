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
    color: MaterialDesignStyling.surface
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5
    property bool ready: false
    property int prev_selection: 0

    onVisibleChanged: {
        if (root.visible) {
            // console.log("WorkSessionsView is now visible.")
            if (!root.ready) {
                //console.log("WorkSessionsView is now becoming ready.")
                root.ready = true
                workSessions.model.setSorting(sortCtl.currentIndex)
                workSessions.model.fetchSome(selectionCtl.currentIndex)
            }
        }

        if (root.ready) {
            workSessions.model.isVisible = root.visible
        }
    }

    Connections {
        target: NaMainTreeModel
        function onSelectedChanged() {
            if (root.visible) {
                //console.log("ActionsListView: Tree selection changed to", NaMainTreeModel.selected)
                selectionCtl.currentIndex = WorkModel.SELECTED_LIST
                workSessions.model.fetchSome(WorkModel.SELECTED_LIST)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        ToolBar {
            Layout.fillWidth: true

            background: Rectangle {
                color: MaterialDesignStyling.surfaceContainer
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StyledComboBox {
                    id: selectionCtl
                    model: ListModel {
                        ListElement { text: qsTr("Today") }
                        ListElement { text: qsTr("Yesterday") }
                        ListElement { text: qsTr("Current Week") }
                        ListElement { text: qsTr("Last Week") }
                        ListElement { text: qsTr("Current Month") }
                        ListElement { text: qsTr("Last Month") }
                        ListElement { text: qsTr("Selected list") }
                    }

                    onActivated: (ix) => {
                        if (prev_selection !== ix) {
                            workSessions.model.fetchSome(ix)
                        }
                        prev_selection = ix
                    }
                }

                StyledComboBox {
                    id: sortCtl
                    currentIndex: 2
                    width: 200
                    displayText: qsTr("Sort by")
                    model: ListModel {
                        ListElement { text: qsTr("change time") }
                        ListElement { text: qsTr("Start time \u25B2") }
                        ListElement { text: qsTr("Start Time \u25BC") }
                    }

                    onActivated: (ix) => {
                        workSessions.model.setSorting(ix)
                    }
                }
            }
        }

        RowLayout {
            WorkSessionList {
                Layout.fillHeight: true
                Layout.preferredWidth: 1000
                id: workSessions
            }

            Rectangle {
                color: "yellow"
                Layout.fillWidth: true;
            }
        }
    }
}
