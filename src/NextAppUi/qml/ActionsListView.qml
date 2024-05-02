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
    anchors.fill: parent
    color: Colors.background
    property bool ready: false
    property int prev_selection: 0

    onVisibleChanged: {
        if (root.visible) {
            console.log("ActionsListView is now visible.")
            if (!root.ready) {
                console.log("ActionsListView is now becoming ready.")
                root.ready = true
            }
        } else {
            console.log("ActionsListView is now hidden");
        }
        ActionsModel.isVisible = root.visible
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
                    currentIndex: ActionsModel.mode
                    Layout.preferredWidth: 220
                    model: ListModel {
                        ListElement { text: qsTr("Today") }
                        ListElement { text: qsTr("Today and overdue") }
                        ListElement { text: qsTr("Current Week") }
                        ListElement { text: qsTr("Current Week and overdue") }
                        ListElement { text: qsTr("Current Month") }
                        ListElement { text: qsTr("Current Month and ocerdue") }
                        ListElement { text: qsTr("Selected list") }
                        ListElement { text: qsTr("Selected list and sublists") }
                        ListElement { text: qsTr("Favorite Actions") }
                    }

                    onActivated: (ix) => {
                        console.log("Selection changed to", ix)
                        ActionsModel.mode = ix
                    }
                }

                Button {
                    text: qsTr("Filter")
                    onClicked: {
                        filter.open()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ActionsList {
                Layout.fillWidth: true
                Layout.fillHeight: true
                id: actions
            }
        }
    }

    ActionsListFilterDlg {
        id: filter

        onApply: {
            console.log("Filter applied")
            ActionsModel.filter = filter.filter
        }
    }
}
