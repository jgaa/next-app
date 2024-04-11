import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB

Rectangle {
    id: root
    color: Colors.background
    property var colwidths: [140, 120, 80, 80, 800]
    property string selectedItem: ""
    property bool selectedIsActive: false

    function somethingChanged() {
        if (selectedItem !== "") {
            if(!WorkSessionsModel.sessionExists(selectedItem)) {
                selectedItem = ""
                selectedIsActive = false
            } else {
                selectedIsActive = WorkSessionsModel.isActive(selectedItem)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        id: rowCtl

        // List of work sessions
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: colwidths.reduce((accumulator, currentValue) => accumulator + currentValue, 0);

            color: Colors.background

            HorizontalHeaderView {
                id: horizontalHeader
                anchors.left: tableView.left
                anchors.top: parent.top
                syncView: tableView
                clip: true
            }

            TableView {
                id: tableView
                model: WorkSessionsModel
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                clip: true
                anchors.top: horizontalHeader.bottom
                anchors.left: horizontalHeader.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                // selectionMode: TableView.SingleSelection
                // selectionBehavior: TableView.SelectRows
                editTriggers: TableView.OnDoubleClicked | TableView.editTriggersOnEditKeyPressed | TableView.OnEnterPressed | TableView.OnF2Pressed

                Component.onCompleted: {
                    console.log("Component.onCompleted")
                    model.onModelReset.connect(function() {
                        console.log("onModelReset")
                        if (root.selectedItem !== "" && !WorkSessionsModel.sessionExists(root.selectedItem)) {
                            console.log("onModelReset clearing selectedItem")
                            root.selectedItem = ""
                        }
                        somethingChanged()
                    })
                }

                columnWidthProvider : function (column) {
                    return root.colwidths[column]
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
                    required property var index
                    property bool selected : root.selectedItem == uuid
                    //TableView.onEditRole: "display"

                    implicitHeight: 30

                    border {
                        color: selected ? Colors.icon: Colors.inactive
                        width: 1
                    }

                    color: selected ? Colors.selection : row % 2 ?  Colors.surface1 : Colors.surface2

                    // Icon
                    RowLayout {
                        Text {
                            leftPadding: 4
                            visible: column === 0
                            text: icon
                            color: Colors.icon
                            font.family: ce.faSolidName
                            font.styleName: ce.faSolidStyle
                            font.pixelSize: 20
                            Layout.fillHeight: true
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            leftPadding: 4
                            verticalAlignment: Text.AlignVCenter
                            text: display
                            color: Colors.text
                            Layout.fillHeight: true
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
                            console.log("Committing: ", text)
                            display = text
                        }
                    }
                }
            }
        }

        // Right buttons
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            Layout.margins: 6

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Pause"
                enabled: root.selectedItem !== "" && root.selectedIsActive

                onClicked: {
                    WorkSessionsModel.pause(root.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "To the Top"
                enabled: root.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.touch(root.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Resume"
                enabled: root.selectedItem !== "" && !root.selectedIsActive

                onClicked: {
                    WorkSessionsModel.resume(root.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Done"
                enabled: root.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.done(root.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Action Completed"
                enabled: root.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.finishAction(root.selectedItem)
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }

    // FontLoader { id: fontAwesome; source: "../fonts/Font Awesome 6 Free-Regular-400.otf" }
    // FontLoader { id: fontAwesomeSolid; source: "../fonts/Font Awesome 6 Free-Solid-900.otf" }

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
           WorkSessionsModel.deleteWork(uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
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
            icon.source: "../icons/fontawsome/trash-can.svg"
            text: qsTr("Delete")
            onTriggered: {
                confirmDelete.uuid = contextMenu.uuid
                confirmDelete.name = contextMenu.name
                confirmDelete.open()
            }
        }
    }
}
