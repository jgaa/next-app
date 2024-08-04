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
    color: MaterialDesignStyling.surface
    property int prev_selection: 0
    enabled: NaComm.connected

    DisabledDimmer {}

    onVisibleChanged: {
        console.log("ActionsView visible changed to", root.visible)
        ActionsModel.isVisible = root.visible
    }

    // onVisibleChanged is not called when the view is part of the initial view stack
    Component.onCompleted: {
        console.log("ActionsView completed")
        ActionsModel.isVisible = root.visible
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
                        ListElement { text: qsTr("On Todays Calendar") }
                    }

                    onActivated: (ix) => {
                        // console.log("Selection changed to", ix)
                        ActionsModel.mode = ix
                    }
                }

                StyledButton {
                    width: 180
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
            // console.log("Filter applied")
            ActionsModel.filter = filter.filter
        }
    }
}
