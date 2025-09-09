import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Window
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models
import "common.js" as Common

Rectangle {
    id: root
    //property string selectedItemUuid: NaMainTreeModel.selected
    property var priorityColors: ["magenta", "red", "orangered", "orange", "green", "blue", "lightblue", "gray"]
    property var statusIcons: ["\uf058", "\uf111", "\uf0c8"]
    property alias model: listView.model
    property alias listCtl: listView
    property bool selectFirstOnModelReset: true
    property var onSelectionEvent: null
    property bool callOnSelectionEvent: typeof onSelectionEvent === "function"
    color: MaterialDesignStyling.surface
    property bool hasReview: false
    property alias selectedIds: listView.selectedItems
    property bool hasSelection: listView.selectedItems.length > 0

    ColumnLayout {
        anchors.fill: parent

        Component {
            id: sectionHeading
            Rectangle {
                width: ListView.view.width  - MaterialDesignStyling.scrollBarWidth
                height: 22
                color: MaterialDesignStyling.primaryContainer

                required property string section

                Text {
                    leftPadding: 10
                    id: label
                    text: parent.section //NaActionsModel.toName(parent.section)
                    font.bold: true
                    color: MaterialDesignStyling.onPrimaryContainer
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        ListView {
            id: listView
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true

            property var selectedItems: []
            signal mySelectedItemsChanged()
            function selectUuid(uuid) {
                if (!selectedItems.includes(uuid)) {
                    selectedItems.push(uuid);
                    mySelectedItemsChanged();
                }
                // console.log("[toggle] The list currently contains: ")
                // for(let i = 0; i < selectedItems.length; i++) {
                //     console.log(" - " + selectedItems[i])
                // }
                hasSelection = selectedItems.length > 0
            }

            function toggleUuid(uuid) {
                //console.log("ActionsList: Toggling selection for " + uuid)
                if (selectedItems.includes(uuid)) {
                    selectedItems.splice(selectedItems.indexOf(uuid), 1); // Remove it
                } else {
                    selectedItems.push(uuid); // Add it
                }

                if (selectedItems.length == 1) {
                    NaActionsModel.selected = selectedItems[0]
                } else {
                    NaActionsModel.selected = ""
                }
                mySelectedItemsChanged();
                // console.log("[toggle] The list currently contains: ")
                // for(let i = 0; i < selectedItems.length; i++) {
                //     console.log(" - " + selectedItems[i])
                // }
                hasSelection = selectedItems.length > 0
            }

            function resetSelection() {
                selectedItems = [];
                mySelectedItemsChanged();
                hasSelection = selectedItems.length > 0
            }

            function isSelected(uuid) {
                //console.log("ActionsList: Checking if " + uuid + " is selected")
                // console.log("The list currently contains: ")
                // for(let i = 0; i < selectedItems.length; i++) {
                //     console.log(" - " + selectedItems[i])
                // }
                return selectedItems.includes(uuid);
            }

            ScrollBar.vertical: ScrollBar {
                id: vScrollBar
                parent: listView
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: MaterialDesignStyling.scrollBarWidth
                policy: ScrollBar.AlwaysOn
            }

            section {
                criteria: ViewSection.FullString
                property: "section"
                delegate: sectionHeading
            }

            Connections {
                target: listView.model

                function onModelReset() {
                    if (root.selectFirstOnModelReset) {
                        console.log("ActionsList: Setting currentIndex to -1 because model was reset and empty")
                        listView.currentIndex = -1
                    }
                    NaActionsModel.selected = ""
                }
            }


            model: NaActionsModel

            delegate: Item {
                id: actionItem
                property bool selected: listView.isSelected(uuid)
                property bool current: listView.currentIndex === index
                property bool deleted: status === 3 //NextappPB.ActionState.DELETED
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property bool favorite
                required property bool hasWorkSession
                required property string listName
                required property string node
                required property string category
                required property string due
                required property int priority
                required property int status
                required property bool reviewed
                required property bool onCalendar
                required property bool workedOnToday
                required property string scoreColor
                required property double score
                required property string tags
                enabled: !deleted

                implicitHeight: row.implicitHeight + 4
                width: listView.width - MaterialDesignStyling.scrollBarWidth
                clip: true

                Connections {
                    target: listView
                    function onMySelectedItemsChanged() {
                        selected = listView.isSelected(uuid)
                    }
                }

                Rectangle {
                    id: background
                    color: actionItem.deleted ? "red" :
                           current
                           ? MaterialDesignStyling.surfaceContainerHighest
                           : done ? MaterialDesignStyling.surfaceContainer
                           : index % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer
                    anchors.fill: parent
                    radius: 8
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onSingleTapped: (eventPoint, button) => {
                        switch (button) {
                            case 0: // touch
                            case Qt.LeftButton:
                                NaCore.clickInitiator = NaCore.ClickInitiator.ACTIONS
                                if (point.modifiers & Qt.ControlModifier) {
                                    listView.toggleUuid(uuid)
                                } else {
                                    listView.resetSelection()
                                    listView.selectUuid(uuid)
                                    listView.currentIndex = index
                                    NaActionsModel.selected = uuid
                                    console.log("ActionsList: NaActionsModel.selected = ", uuid)
                                    if (root.callOnSelectionEvent) {
                                        root.onSelectionEvent(uuid)
                                    }
                                    NaCore.currentActionSelected(uuid)
                                }
                                break;
                            case Qt.RightButton:
                                contextMenu.uuid = uuid
                                contextMenu.name = name
                                contextMenu.popup();
                        }
                    }
                    onLongPressed: {
                        contextMenu.uuid = uuid
                        contextMenu.name = name
                        contextMenu.popup()
                    }
                }

                DragHandler {
                    id: dragHandler
                    target: actionItem
                    enabled: NaCore.dragEnabled
                    property real origX: actionItem.x
                    property real origY: actionItem.y

                    onActiveChanged: {
                        if (active) {
                            actionItem.opacity = 0.5
                            dragHandler.origX = actionItem.x
                            dragHandler.origY = actionItem.y
                            actionItem.grabToImage(function(result) {
                                // TODO: Crop the image to a max width in C++ and provide a new url for it
                                parent.Drag.imageSource = result.url
                                parent.Drag.active = true
                            })
                        } else {
                            actionItem.opacity = 1
                            actionItem.x = dragHandler.origX
                            actionItem.y = dragHandler.origY
                            parent.Drag.active = false
                        }
                    }
                }

                //Drag.active: dragHandler.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.MoveAction
                Drag.mimeData: {
                    "text/app.nextapp.action": actionItem.uuid,
                    "text/app.nextapp.curr.node": actionItem.node
                }

                RowLayout {
                    id: fullRow
                    Layout.fillWidth: true
                    spacing: 2

                    SelectedIndicatorBar {
                        selected: actionItem.selected
                    }

                ColumnLayout {
                    id: row
                    Layout.fillWidth: true

                    RowLayout {
                        spacing: 6

                        CheckBoxWithFontIcon {
                            id: revewedIcon
                            visible: root.hasReview
                            isChecked: reviewed
                            checkedCode: "\uf06e"
                            uncheckedCode: "\uf06e"
                            checkedColor: "green"
                            uncheckedColor: "blue"
                            useSolidForChecked: true
                            iconSize: 16
                            autoToggle: false
                            text: ""

                            onClicked: {
                                listView.model.toggleReviewed(uuid)
                            }
                        }

                        CheckBoxWithFontIcon {
                            Layout.topMargin: 2
                            Layout.bottomMargin: 2
                            isChecked: done
                            checkedCode: "\uf058"
                            uncheckedCode: root.statusIcons[status]
                            checkedColor: "green"
                            uncheckedColor: "orange"

                            onClicked: {
                                NaActionsModel.markActionAsDone(uuid, isChecked)
                            }

                            Text {
                                anchors.fill: parent
                                font.family: ce.faSolidName
                                font.styleName: ce.faSolidStyle
                                text: "\uf06d"
                                color: scoreColor //root.priorityColors[priority]
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            //bgColor: onCalendar ? MaterialDesignStyling.primaryContainer : "transparent"
                        }


                        Rectangle {
                            width: 12
                            height: 20
                            color: NaAcModel.valid ? NaAcModel.getColorFromUuid(actionItem.category) : "lightgray"
                            visible: actionItem.category !== ""
                        }

                        Text {
                            id: actionName
                            text: name
                            color: done ? MaterialDesignStyling.onSurfaceVariant : MaterialDesignStyling.onSurface

                            Component.onCompleted: {
                                font.pointSize *= 1.15;
                            }
                        }

                        CheckBoxWithFontIcon {
                            id: favoriteIcon
                            enabled: !done
                            isChecked: favorite
                            checkedCode: "\uf005"
                            uncheckedCode: "\uf005"
                            checkedColor: "orange"
                            uncheckedColor: "lightgray"
                            useSolidForChecked: true
                            iconSize: 16
                            autoToggle: false
                            text: ""

                            onClicked: {
                                NaActionsModel.markActionAsFavorite(uuid, !favorite)
                            }
                        }

                        CheckBoxWithFontIcon {
                            id: canWorkIcon
                            enabled: !done
                            isChecked: hasWorkSession
                            checkedCode: "\uf017"
                            uncheckedCode: "\uf017"
                            checkedColor: "green"
                            uncheckedColor: "lightblue"
                            useSolidForChecked: true
                            iconSize: 16
                            autoToggle: false
                            useSolidForAll: workedOnToday

                            onClicked: {
                                NaWorkSessionsModel.startWorkSetActive(uuid)
                            }
                        }
                    }

                    RowLayout {
                        Item {
                            visible: !onCalendarLabel.visible
                            Layout.preferredWidth: 20
                        }

                        Label {
                            id: onCalendarLabel
                            visible: onCalendar
                            font.family: ce.faSolidName
                            font.styleName: ce.faSolidStyle
                            text: "\uf783"
                            color: MaterialDesignStyling.onPrimaryContainer
                            font.pixelSize: listLabel.font.pixelSize
                            Layout.preferredWidth: 20
                        }

                        Label {
                            visible: dueLabel.visible
                            font.family: ce.faNormalName
                            text: "\uf133"
                            color: MaterialDesignStyling.onSurface
                            font.pixelSize: listLabel.font.pixelSize
                        }

                        Label {
                            id: dueLabel
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: due
                        }

                        Item {
                            visible: dueLabel.visible
                            Layout.preferredWidth: 4
                        }

                        Label {
                            visible: listLabel.visible
                            font.family: ce.faSolidName
                            font.styleName: ce.faSolidStyle
                            text: "\uf802"
                            color: MaterialDesignStyling.onSurface
                            font.pixelSize: listLabel.font.pixelSize
                        }

                        Label {
                            id: listLabel
                            color: MaterialDesignStyling.outline
                            text: listName
                            visible: text !== ""
                        }


                        // Label {
                        //     color: MaterialDesignStyling.onSurfaceVariant
                        //     text: "score"
                        // }
                        // Label {
                        //     color: MaterialDesignStyling.outline
                        //     text: score
                        // }
                    }

                    RowLayout {
                        Item {
                            visible: tagsCtl.visible
                            Layout.preferredWidth: 20
                        }

                        Label {
                            visible: tagsCtl.visible
                            font.family: ce.faSolidName
                            font.styleName: ce.faSolidStyle
                            text: "\uf02c"
                            color: MaterialDesignStyling.onSurface
                            font.pixelSize: listLabel.font.pixelSize
                        }

                        Label {
                            id: tagsCtl
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: tags
                            visible: text !== ""
                        }
                    }
                }
                }
            }
        }
    }

    MyMenu {
        id: contextMenu
        property string uuid
        property string name

        Action {
            text: qsTr("Edit")
            icon.source: "../icons/fontawsome/pen-to-square.svg"
            onTriggered: {
                openActionDlg(contextMenu.uuid)
            }
        }
        Action {
            text: qsTr("Statistics")
            onTriggered: {
                Common.openDialog("ActionStatsDlg.qml", root, {
                    model: ModelInstances.getActionStatsModel(contextMenu.uuid)
                })
            }
        }
        Action {
            text: qsTr("Start Work Session")
            icon.source: "../icons/fontawsome/clock.svg"
            onTriggered: {
                NaWorkSessionsModel.startWork(contextMenu.uuid)
            }
        }
        Action {
            text: qsTr("Add completed Work Session")
            icon.source: "../icons/fontawsome/clock.svg"
            onTriggered: {
                openAddWorkDialog(contextMenu.uuid, contextMenu.name)
            }
        }
        Action {
            icon.source: "../icons/fontawsome/trash-can.svg"
            text: qsTr("Delete")
            onTriggered: {
                confirmDelete.uuid = contextMenu.uuid
                confirmDelete.name = contextMenu.name
                confirmDelete.open()
            }
        }
    }

    CommonElements {
        id: ce
    }

    MessageDialog {
        id: confirmDelete

        property string uuid;
        property string name;

        title: qsTr("Do you really want to delete the Action \"%1\" ?").arg(name)
        text: qsTr("Note that any sub-items and all related information, including worked time, etc. will also be deleted! This action can not be undone.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           NaActionsModel.deleteAction(uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
        }
    }

    function openActionDlg(uuid) {
        Common.openDialog("EditActionDlg.qml", ApplicationWindow.window, {
            node: NaMainTreeModel.selected,
            title: qsTr("Edit Action"),
            aprx: NaActionsModel.getAction(uuid)
        });
    }

    function openAddWorkDialog(uuid, name) {
        Common.openDialog("EditWorkSession.qml", ApplicationWindow.window, {
            ws: NaWorkSessionsModel.createSession(uuid, name),
            title: qsTr("Add Work Session"),
            model: NaWorkSessionsModel
        });
    }
}
