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
    property var colwidths: [120, 60, 60, 60]
    property alias model: tableView.model
    property string selectedItem: ""
    property bool selectedIsActive: false
    property bool selectedIsStarted: false
    color: MaterialDesignStyling.surface
    Layout.fillHeight: true

    function somethingChanged() {
        if (selectedItem !== "") {
            if(!model.sessionExists(selectedItem)) {
                selectedItem = ""
                selectedIsActive = false
                selectedIsStarted = false
            } else {
                selectedIsActive = tableView.model.isActive(selectedItem)
                selectedIsStarted = tableView.model.isStarted(selectedItem)
            }
        }
    }

    function remainingColWidth(availWidth) {
        var totalWidth = colwidths.reduce((accumulator, currentValue) => accumulator + currentValue, 0);
        return availWidth - totalWidth
    }

    StyledHeaderView {
        id: horizontalHeader
        anchors.left: tableView.left
        anchors.top: root.top
        syncView: tableView
        //visible: tableView.model && tableView.model.rowCount > 0
    }

    ScrollView {
        anchors.top: horizontalHeader.bottom
        anchors.left: horizontalHeader.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        TableView {
            id: tableView
            anchors.fill: parent
            model: NaCore.createWorkModel()
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true

            editTriggers: TableView.OnDoubleClicked | TableView.editTriggersOnEditKeyPressed | TableView.OnEnterPressed | TableView.OnF2Pressed

            Component.onCompleted: {
                // console.log("WorkSessionList.onCompleted")
                model.onModelReset.connect(function() {
                    // console.log("WorkSessionList.onModelReset")
                    if (root.selectedItem !== "" && !model.sessionExists(root.selectedItem)) {
                        // console.log("WorkSessionList.onModelReset clearing selectedItem")
                        root.selectedItem = ""
                    }
                    somethingChanged()
                })
            }

            onVisibleChanged: {
                console.log("WorkSessionList.onVisibleChanged: ", tableView.visible, ", ", model.isVisible)
                model.isVisible = tableView.visible
            }

            selectionModel: ItemSelectionModel {}

            delegate : Rectangle {
                id: delegate
                required property int row
                required property int column
                required property var display
                required property string uuid
                required property string icon
                required property bool active
                required property string action
                required property var index
                property bool selected : root.selectedItem == uuid

                implicitWidth: column == 4 ? remainingColWidth(tableView.width - 10) : root.colwidths[column]
                implicitHeight: 30

                border {
                    color: MaterialDesignStyling.outlineVariant
                    width: 1
                }

                color: selected
                       ? MaterialDesignStyling.surfaceContainerHighest
                       : row % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer


                RowLayout {
                    implicitHeight: delegate.implicitHeight
                    width: parent.width
                    clip: true

                    SelectedIndicatorBar {
                        selected: column === 0 && delegate.selected
                    }

                    Text {
                        Layout.leftMargin: 4
                        visible: column === 0
                        text: icon
                        color: MaterialDesignStyling.onSurfaceVariant
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 20
                        Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter
                    }

                    Text {
                        Layout.leftMargin: 4
                        verticalAlignment: Text.AlignVCenter
                        text: display
                        color: MaterialDesignStyling.onSurface
                        Layout.fillHeight: true
                    }

                    Text {
                        id: notesIcon
                        Layout.margins: 4
                        visible: column === 4 && hasNotes
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        text: "\uf304"
                        color: MaterialDesignStyling.onSurfaceVariant
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onSingleTapped: (eventPoint, button) => {
                        switch (button) {
                            case Qt.LeftButton:
                                root.selectedItem = uuid
                                somethingChanged()
                                break;
                            case Qt.RightButton:
                                contextMenu.actionid = action
                                contextMenu.uuid = uuid
                                contextMenu.name = display
                                contextMenu.popup();
                        }
                    }

                    onDoubleTapped: (eventPoint, button) => {
                        if (column === 3 /* used */) {
                            return;
                        }

                        var ix = tableView.index(row, column)
                        tableView.edit(ix);
                    }

                    onLongPressed: (eventPoint, button) => {
                        contextMenu.actionid = action
                        contextMenu.uuid = uuid
                        contextMenu.name = display
                        contextMenu.popup();
                    }
                }

                TableView.editDelegate: TextField {
                    anchors.fill: parent
                    text: display
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    Component.onCompleted: selectAll()

                    TableView.onCommit: {
                        // console.log("Committing: ", text)
                        display = text
                    }
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

            title: qsTr("Do you really want to delete the Work Session \"%1\" ?").arg(name)
            text: qsTr("Note that any worked time, etc. for this session will also be deleted! This action can not be undone.")
            buttons: MessageDialog.Ok | MessageDialog.Cancel
            onAccepted: {
               tableView.model.deleteWork(uuid)
               confirmDelete.close()
            }

            onRejected: {
                confirmDelete.close()
            }
        }
    }

    MyMenu {
        id: contextMenu
        property string actionid
        property string uuid
        property string name

        Action {
            text: qsTr("Edit session")
            icon.source: "../icons/fontawsome/pen-to-square.svg"
            onTriggered: {
                openWorkSessionDlg(contextMenu.uuid)
            }
        }
        Action {
            icon.source: "../icons/fontawsome/trash-can.svg"
            text: qsTr("Delete session")
            onTriggered: {
                confirmDelete.uuid = contextMenu.uuid
                confirmDelete.name = contextMenu.name
                confirmDelete.open()
            }
        }
        MenuSeparator {}
        Action {
            text: qsTr("Edit action")
            //icon.source: "../icons/fontawsome/play.svg"
            onTriggered: {
                openActionDlg(contextMenu.actionid)
            }
        }
    }

    function openWorkSessionDlg(uuid) {
        openDialog("EditWorkSession.qml", {
            title: qsTr("Edit Work Session"),
            ws: tableView.model.getSession(uuid),
            model: tableView.model
        });
    }

    function openActionDlg(uuid) {
        openDialog("EditActionDlg.qml", {
            title: qsTr("Edit Action"),
            aprx: NaActionsModel.getAction(uuid)
        });
    }
}
