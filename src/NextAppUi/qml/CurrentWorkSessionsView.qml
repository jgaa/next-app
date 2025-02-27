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
    color: MaterialDesignStyling.surfaceContainer
    Layout.fillWidth: true
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5

    RowLayout {
        anchors.fill: parent
        id: rowCtl

        WorkSessionList {
            Layout.preferredWidth: 800
            Layout.minimumWidth: 300
            id: workSessionList
            model: NaWorkSessionsModel
        }

        // Right buttons
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            Layout.minimumWidth: 200
            Layout.margins: 6

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: qsTr("Pause")
                enabled: workSessionList.selectedItem !== "" && workSessionList.selectedIsActive

                onClicked: {
                    NaWorkSessionsModel.pause(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: qsTr("To the Top")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.touch(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: workSessionList.selectedIsStarted ? qsTr("Resume") : qsTr("Start")
                enabled: workSessionList.selectedItem !== "" && !workSessionList.selectedIsActive

                onClicked: {
                    NaWorkSessionsModel.resume(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: qsTr("Done")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.done(workSessionList.selectedItem)
                }
            }

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: qsTr("Action Completed")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.finishAction(workSessionList.selectedItem)
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

    DropArea {
        anchors.fill: parent

        onEntered: (drag) => {
            // console.log("WorkSessionsView/DropArea entered by ", drag.source.toString(), " types ", drag.formats)
            if (drag.formats.indexOf("text/app.nextapp.calendar.event") !== -1) {
                drag.accepted = true
            }
        }

        onDropped: (drop) => {
            if (drop.formats.indexOf("text/app.nextapp.calendar.event") !== -1) {
                let uuid = drop.getDataAsString("text/app.nextapp.calendar.event")
                // console.log("WorkSessionsViewDropped calendar event ", uuid, " at x=", drop.x, ", y=", drop.y)

                NaWorkSessionsModel.addCalendarEvent(uuid)
                drop.accepted = true
            }
        }
    }

}
