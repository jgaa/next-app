import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models
import "common.js" as Common

Rectangle {
    id: root
    anchors.fill: parent
    property string selectedItemUuid: NaMainTreeModel.selected

    color: MaterialDesignStyling.surface

    ColumnLayout {
        anchors.fill: parent

        Component {
            id: sectionHeading
            Rectangle {
                width: ListView.view.width
                height: 22
                color: MaterialDesignStyling.primary

                required property string section

                Text {
                    id: label
                    text: ActionsModel.toName(parent.section)
                    font.bold: true
                    color: MaterialDesignStyling.onPrimary
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

            section {
                criteria: ViewSection.FullString
                property: "section"
                delegate: sectionHeading
            }

            model: ActionsModel

            delegate: Item {
                id: actionItem
                property bool selected: listView.currentIndex === index
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property bool favorite
                required property bool hasWorkSession
                required property string listName
                required property string node
                required property string category

                implicitHeight: row.implicitHeight + 4
                width: listView.width
                clip: true

                Rectangle {
                    id: background
                    color: selected
                           ? MaterialDesignStyling.surfaceContainerHighest
                           : done ? MaterialDesignStyling.surfaceContainer
                           : index % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer
                    anchors.fill: parent
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onSingleTapped: (eventPoint, button) => {
                        switch (button) {
                            case 0: // touch
                            case Qt.LeftButton:
                                listView.currentIndex = index
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
                    enabled: !NaCore.isMobile
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
                Drag.dragType: NaCore.isMobile ? Drag.None :  Drag.Automatic
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
                            Layout.topMargin: 2
                            Layout.bottomMargin: 2
                            isChecked: done
                            checkedCode: "\uf058"
                            uncheckedCode: "\uf111"
                            checkedColor: "green"
                            uncheckedColor: "orange"

                            onClicked: {
                                ActionsModel.markActionAsDone(uuid, isChecked)
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 20
                            color: ActionCategoriesModel.valid ? ActionCategoriesModel.getColorFromUuid(actionItem.category) : "lightgray"
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
                                ActionsModel.markActionAsFavorite(uuid, !favorite)
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

                            onClicked: {
                                WorkSessionsModel.startWork(uuid)
                            }
                        }
                    }

                    RowLayout {
                        Item {
                            Layout.preferredWidth: 20
                        }

                        Label {
                            color: MaterialDesignStyling.outline
                            text: listName
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
            text: qsTr("Start Work Session")
            icon.source: "../icons/fontawsome/clock.svg"
            onTriggered: {
                WorkSessionsModel.startWork(contextMenu.uuid)
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

    MessageDialog {
        id: confirmDelete

        property string uuid;
        property string name;

        title: qsTr("Do you really want to delete the Action \"%1\" ?").arg(name)
        text: qsTr("Note that any sub-items and all related information, including worked time, etc. will also be deleted! This action can not be undone.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           ActionsModel.deleteAction(uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
        }
    }

    function openActionDlg(uuid) {
        Common.openDialog("EditActionDlg.qml", root, {
            node: mainTree.selectedItemUuid,
            title: qsTr("Edit Action"),
            aprx: ActionsModel.getAction(uuid)
        });
    }

    function openAddWorkDialog(uuid, name) {
        Common.openDialog("EditWorkSession.qml", root, {
            ws: WorkSessionsModel.createSession(uuid, name),
            title: qsTr("Add Work Session"),
            model: WorkSessionsModel
        });
    }
}
