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
    color: Colors.background
    property bool ready: false
    property int prev_selection: 0

    Component.onCompleted: {
        console.log("WorkSessionsView is now completed");
        console.log("Gakke is a ", Gakke)
    }

    onVisibleChanged: {
        if (root.visible) {
            console.log("WorkSessionsView is now visible.")
            if (!root.ready) {
                console.log("WorkSessionsView is now becoming ready.")
                root.ready = true
                workSessions.model = NaCore.createWorkModel()
                //workSessions.model.setDebug(true)
                workSessions.model.doStart()
                workSessions.model.isVisible = true
                workSessions.model.setSorting(sortCtl.currentIndex)
                workSessions.model.fetchSome(selectionCtl.currentIndex)
            }
        } else {
            console.log("Component is now hidden");
        }

        if (root.ready) {
            workSessions.model.isVisible = root.visible
        }
    }

    ColumnLayout {
        anchors.fill: parent

        ToolBar {
            Layout.fillWidth: true

            background: Rectangle {
                color: Colors.background
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ComboBox {
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

                    onAccepted: {
                        //workSessions.model.fetchSome(selectionCtl.currentIndex)
                    }

                    onActivated: (ix) => {
                        if (prev_selection !== ix) {
                            workSessions.model.fetchSome(ix)
                        }
                        prev_selection = ix
                    }
                }


                ComboBox {
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
                //Layout.fillWidth: true
                id: workSessions
            }

            Item {
                Layout.fillWidth: true;
            }
        }
    }
}
