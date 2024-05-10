import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB

Rectangle {
    id: root
    color: MaterialDesignStyling.surfaceContainer
    Layout.fillWidth: true

    RowLayout {
        anchors.fill: parent
        id: rowCtl

        WorkSessionList {
            Layout.preferredWidth: 800
            Layout.minimumWidth: 300
            id: workSessionList
            model: WorkSessionsModel
        }

        // Right buttons
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            Layout.minimumWidth: 200
            Layout.margins: 6

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: "Pause"
                enabled: workSessionList.selectedItem !== "" && workSessionList.selectedIsActive

                onClicked: {
                    WorkSessionsModel.pause(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: "To the Top"
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.touch(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: "Resume"
                enabled: workSessionList.selectedItem !== "" && !workSessionList.selectedIsActive

                onClicked: {
                    WorkSessionsModel.resume(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: "Done"
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    WorkSessionsModel.done(workSessionList.selectedItem)
                }
            }

            StyledButton {
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
