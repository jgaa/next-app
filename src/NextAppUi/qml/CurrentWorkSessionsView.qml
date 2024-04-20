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

    RowLayout {
        anchors.fill: parent
        id: rowCtl

        WorkSessionList {
            id: workSessionList
            model: WorkSessionsModel
        }

        // Right buttons
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            Layout.margins: 6

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Pause"
                enabled: workSessionList.selectedItem !== "" && workSessionList.selectedIsActive

                onClicked: {
                    WorkSessionsModel.pause(workSessionList.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "To the Top"
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.touch(workSessionList.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Resume"
                enabled: workSessionList.selectedItem !== "" && !workSessionList.selectedIsActive

                onClicked: {
                    WorkSessionsModel.resume(workSessionList.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Done"
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.done(workSessionList.selectedItem)
                }
            }

            Button {
                implicitHeight: 22 // Adjust as needed
                text: "Action Completed"
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.finishAction(workSessionList.selectedItem)
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
}
