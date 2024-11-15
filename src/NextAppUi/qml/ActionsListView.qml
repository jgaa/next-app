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
        //console.log("ActionsView visible changed to", root.visible)
        NaActionsModel.isVisible = root.visible
    }

    // onVisibleChanged is not called when the view is part of the initial view stack
    Component.onCompleted: {
        //console.log("ActionsView completed")
        NaActionsModel.isVisible = root.visible
    }

    Connections {
        target: NaMainTreeModel
        onSelectedChanged: {
            if (root.visible) {
                //console.log("ActionsListView: Tree selection changed to", NaMainTreeModel.selected)
                if (NaMainTreeModel.selected != ""
                        && NaActionsModel.mode !== NaActionsModel.FW_SELECTED_NODE
                        && NaActionsModel.mode !== NaActionsModel.FW_SELECTED_NODE_AND_CHILDREN) {
                    selectionCtl.currentIndex = NaActionsModel.FW_SELECTED_NODE
                    NaActionsModel.mode = NaActionsModel.FW_SELECTED_NODE
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        ToolBar {
            id: headerCtl
            Layout.fillWidth: true
            property int comboWidth: (width - (2 * 4) - refreschButton.width) / 2

            background: Rectangle {
                color: MaterialDesignStyling.surfaceContainer
            }

            RowLayout {
                anchors.fill: parent
                spacing: 4
                StyledComboBox {
                    id: selectionCtl
                    currentIndex: NaActionsModel.mode
                    Layout.preferredWidth: headerCtl.comboWidth

                    model: ListModel {
                        ListElement { text: qsTr("Active") }
                        ListElement { text: qsTr("Today") }
                        ListElement { text: qsTr("Today and overdue") }
                        ListElement { text: qsTr("Tomorrow") }
                        ListElement { text: qsTr("Current Week") }
                        ListElement { text: qsTr("Next week") }
                        ListElement { text: qsTr("Current Month") }
                        ListElement { text: qsTr("Next Month") }
                        ListElement { text: qsTr("Selected list") }
                        ListElement { text: qsTr("Selected list and sublists") }
                        ListElement { text: qsTr("Favorite Actions") }
                        ListElement { text: qsTr("On the Calendar") }
                        ListElement { text: qsTr("Unassigned") }
                        ListElement { text: qsTr("On hold") }
                        ListElement { text: qsTr("Completed") }
                    }

                    onActivated: (ix) => {
                        // console.log("Selection changed to", ix)
                        NaActionsModel.mode = ix
                    }
                }

                StyledComboBox {
                    id: sortingCtl
                    currentIndex: NaActionsModel.sort
                    Layout.preferredWidth: headerCtl.comboWidth
                    implicitContentWidthPolicy: ComboBox.widestTextWhenCompleted
                    model: ListModel {
                        ListElement { text: qsTr("Default") }
                        ListElement { text: qsTr("Priority, Start Date, Name") }
                        ListElement { text: qsTr("Priority, Due Date, Name") }
                        ListElement { text: qsTr("Start Date, Name") }
                        ListElement { text: qsTr("Due Date, Name") }
                        ListElement { text: qsTr("Name") }
                        ListElement { text: qsTr("Created Date") }
                        ListElement { text: qsTr("Created Date Desc") }
                        ListElement { text: qsTr("Completed Date") }
                        ListElement { text: qsTr("Completed Date Desc") }
                    }

                    onActivated: (ix) => {
                        // console.log("Sorting changed to", ix)
                        NaActionsModel.sort = ix
                    }
                }

                StyledButton {
                    id: refreschButton
                    Layout.preferredWidth: parent.height
                    Layout.minimumWidth: 20
                    //text: qsTr("Refresh")
                    text: "";
                    onClicked: {
                        NaActionsModel.refresh()
                    }

                    Image {
                        id: refreschIcon
                        source: "qrc:/qt/qml/NextAppUi/icons/refresh.svg"
                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(18, 18)
                        anchors.centerIn: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        ActionsList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            id: actions
        }
    }

    ActionsListFilterDlg {
        id: filter

        onApply: {
            // console.log("Filter applied")
            NaActionsModel.filter = filter.filter
        }
    }
}
