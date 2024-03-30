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

            color: "yellow" //Colors.background

            TableView {
                id: tableView
                model: WorkSessionsModel
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                clip: true
                anchors.fill: parent
                selectionMode: TableView.SingleSelection
                selectionBehavior: TableView.SelectRows

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

                delegate : Rectangle {
                    required property int row
                    required property int column
                    required property var display
                    required property string uuid
                    required property string icon
                    required property bool active
                    property bool selected : root.selectedItem == uuid

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
                        }

                        Text {
                            leftPadding: 4
                            verticalAlignment: Text.AlignVCenter
                            text: display
                            color: Colors.text
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.selectedItem = uuid
                            somethingChanged()
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
                text: "New"
            }

            Item {
                Layout.fillHeight: true
            }

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
}
