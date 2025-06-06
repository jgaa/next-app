import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Rectangle {
    id: root
    //anchors.fill: parent
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
        function onSelectedChanged() {
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

            background: Rectangle {
                color: MaterialDesignStyling.surfaceContainer
            }

            RowLayout {
                id: row
                anchors.fill: parent
                spacing: 4
                StyledComboBox {
                    id: selectionCtl
                    currentIndex: NaActionsModel.mode
                    Layout.minimumWidth: 40
                    Layout.maximumWidth: 250
                    Layout.fillWidth: true
                    // maxWidth: (root.width
                    //                         - selectionsButton.width
                    //                         - filterButton.width
                    //                         - refreschButton.width
                    //                         - row.spacing * 3) / 2

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
                        console.log("Selection changed to", ix,
                                    ", row.width=", row.width, ", root.width=", root.width, ", selectionCtl.width=", selectionCtl.width)
                        NaActionsModel.mode = ix
                    }
                }

                StyledComboBox {
                    id: sortingCtl
                    currentIndex: NaActionsModel.sort
                    Layout.minimumWidth: 40
                    Layout.maximumWidth: 250
                    Layout.fillWidth: true
                    // maxWidth: (root.width
                    //                         - selectionsButton.width
                    //                         - filterButton.width
                    //                         - refreschButton.width
                    //                         - row.spacing * 3) / 2
                    implicitContentWidthPolicy: ComboBox.ContentItemImplicitWidth
                    model: ListModel {
                        ListElement { text: qsTr("Default") }
                        ListElement { text: qsTr("Priority, Start Date, Name") }
                        ListElement { text: qsTr("Priority, Due Date, Name") }
                        ListElement { text: qsTr("Start Date, Name") }
                        ListElement { text: qsTr("Due Date, Name") }
                        ListElement { text: qsTr("Category, Name") }
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
                    id: selectionsButton
                    enabled: actions.hasSelection
                    Layout.preferredHeight: parent.height
                    //Layout.minimumWidth: 20
                    useWidth: 30
                    text: "";
                    onClicked: {
                        menu.open()
                    }

                    Image {
                        id: multiSelectIcon
                        source: "qrc:/qt/qml/NextAppUi/icons/multiselection-menu.svg"
                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(18, 18)
                        anchors.centerIn: parent
                    }

                    Menu {
                        id: menu
                        title: qsTr("On selection(s)")
                        MenuItem {
                            text: qsTr("Change when")
                            onTriggered: {
                                changeWhenDlg.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Set Category")
                            onTriggered: {
                                setCategoryDlg.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Set Priority")
                            onTriggered: {
                                setPriorityDlg.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Set Difficulty")
                            onTriggered: {
                                setDifficultyDlg.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Delete")
                            onTriggered: {
                                deleteDlg.open()
                            }
                        }
                    }
                }

                StyledButton {
                    id: filterButton
                    Layout.preferredHeight: parent.height
                    //Layout.minimumWidth: 20
                    useWidth: 30
                    //text: qsTr("Filter")
                    text: "";
                    dim: NaActionsModel.filtersEnabled
                    onClicked: {
                        NaActionsModel.filtersEnabled = !NaActionsModel.filtersEnabled
                    }

                    Image {
                        id: filterIcon
                        source: "qrc:/qt/qml/NextAppUi/icons/filter.svg"
                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(18, 18)
                        anchors.centerIn: parent
                    }
                }

                StyledButton {
                    id: refreschButton
                    Layout.preferredHeight: parent.height
                    //Layout.minimumWidth: 20
                    useWidth: 30
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
                    Layout.minimumWidth: 0
                    Layout.fillWidth: true
                }
            }
        }

        ToolBar {
            id: filtersCtl
            visible: NaActionsModel.filtersEnabled
            Layout.fillWidth: true
            Layout.preferredHeight: headerCtl.height

            background: Rectangle {
                color: MaterialDesignStyling.surfaceContainer
            }

            Item {
                Layout.preferredWidth: 8
            }

            RowLayout {
                anchors.fill: parent
                spacing: 4
                TextInput {
                    id: searchCtl
                    Layout.preferredWidth: parent.width * 0.6
                    Layout.preferredHeight: headerCtl.height - 10
                    Layout.alignment: Qt.AlignVCenter
                    leftPadding: 8
                    rightPadding: 8
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: MaterialDesignStyling.onPrimaryContainer
                    //text: NaActionsModel.match

                    onTextChanged: {
                        NaActionsModel.match = searchCtl.text
                    }

                    Rectangle {
                        anchors.fill : parent
                        color: MaterialDesignStyling.primaryContainer
                        border.color: MaterialDesignStyling.onPrimaryFixed
                        border.width: 1
                        z: -1
                    }

                    Text {
                        id: searchIcon
                        visible: searchCtl.text === ""
                        anchors.right: parent.right
                        //anchors.centerIn: parent
                        verticalAlignment: Text.AlignVCenter
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        text: "\uf002"
                        //width: parent.height
                        height: parent.height
                        color: MaterialDesignStyling.onSecondaryContainer
                        rightPadding: 4
                    }

                    Text {
                        id: clearBtn
                        visible: !searchIcon.visible
                        anchors.right: parent.right
                        verticalAlignment: Text.AlignVCenter
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        text: "\uf2d3"
                        //width: parent.height
                        height: parent.height
                        color: MaterialDesignStyling.onSecondaryContainer
                        rightPadding: 4

                        MouseArea {
                            // set hand mourse
                            cursorShape: Qt.PointingHandCursor
                            anchors.fill: parent
                            onClicked: {
                                searchCtl.text = ""
                            }
                        }
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

    Dialog {
        title: qsTr("Change when")
        id: changeWhenDlg
        width: 300
        height: 200
        property alias due: whenCtl.due

        // Show cancel button
        standardButtons: Dialog.Cancel

        onVisibleChanged: {
            if (visible) {
                whenCtl.reset()
                moveToCtl.reset()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            WhenSelector {
                Layout.fillWidth: true
                id: whenCtl
                onDueWasSelected: (due) => {
                    NaActionsModel.batchChangeDue(due, actions.selectedIds)
                    changeWhenDlg.close()
                }
            }

            MoveDue {
                Layout.fillWidth: true
                id: moveToCtl
                onDueValueChanged: (due) => {
                    NaActionsModel.batchChangeDue(due, actions.selectedIds)
                    changeWhenDlg.close()
                }
            }
        }
    }

    Dialog {
        title: qsTr("Set Category")
        id: setCategoryDlg
        width: 300
        height: 150

        // Show cancel button
        standardButtons: Dialog.Cancel

        onVisibleChanged: {
            if (visible) {
                category.reset()
            }
        }

        CategoryComboBox {
            id: category
            Layout.leftMargin: root.leftMarginForControls
            Layout.fillWidth: true

            onCategorySelected: (category) => {
                NaActionsModel.batchChangeCategory(category, actions.selectedIds)
                setCategoryDlg.close()
            }
        }
    }

    Dialog {
        title: qsTr("Set Priority")
        id: setPriorityDlg
        width: 300
        height: 150

        // Show cancel button
        standardButtons: Dialog.Cancel

        onVisibleChanged: {
            if (visible) {
                priority.reset()
            }
        }

        PrioritySelector {
            id: priority
            Layout.leftMargin: root.leftMarginForControls
            Layout.fillWidth: true

            onPriorityChanged: (pri) => {
                NaActionsModel.batchChangePriority(pri, actions.selectedIds)
                setPriorityDlg.close()
            }
        }
    }

    Dialog {
        title: qsTr("Set Difficulty")
        id: setDifficultyDlg
        width: 300
        height: 150

        // Show cancel button
        standardButtons: Dialog.Cancel

        onVisibleChanged: {
            if (visible) {
                difficulty.reset()
            }
        }

        DifficultySelector {
            id: difficulty
            Layout.leftMargin: root.leftMarginForControls
            Layout.fillWidth: true

            onDifficultyChanged: (diff) => {
                if (!setDifficultyDlg.visible) {
                    return
                }
                NaActionsModel.batchChangeDifficulty(diff, actions.selectedIds)
                setDifficultyDlg.close()
            }
        }
    }

    Dialog {
        id: deleteDlg
        title: qsTr("Delete Actions")
        width: 300
        height: 200
        standardButtons: Dialog.Cancel | Dialog.Ok
        Text {
            text: qsTr("Are you sure you want to delete the selected actions?\nThis action cannot be undone.")
            wrapMode: Text.WordWrap
            clip: true
        }

        onAccepted: {
            NaActionsModel.batchDelete(actions.selectedIds)
        }
    }

    CommonElements {
        id: ce
    }
}
