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
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5
    property alias minHeight: buttonsCtl.implicitHeight

    RowLayout {
        anchors.fill: parent
        id: rowCtl

        WorkSessionList {
            //Layout.preferredWidth: 800
            //Layout.minimumWidth: 300
            Layout.fillWidth: true
            id: workSessionList
            model: NaWorkSessionsModel
            enableSelectAction: true
        }

        // Right buttons
        ColumnLayout {
            id: buttonsCtl
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            //Layout.minimumWidth: 200
            Layout.margins: 6
            // readonly property int maxButtonWidth: Math.max(
            //         startButton.implicitWidth,
            //         doneButton.implicitWidth,
            //         actionCompletedButton.implicitWidth,
            //         toTheTopButton.implicitWidth
            //     )

            StyledButton {
                property int mode: // 0  Nothing selected  1 start 2 continue 3 pause
                    workSessionList.selectedItem !== "" ? (workSessionList.selectedIsActive ? 3 : workSessionList.selectedIsStarted ? 2 : 1) : 0
                property var currenText: [qsTr("Start"), qsTr("Start"), qsTr("Resume"), qsTr("Pause")];
                implicitHeight: 22 // Adjust as needed
                text: currenText[mode]
                enabled: mode > 0

                onClicked: {
                    console.log("WorkSessionView/Start/Continue/Pause clicked mode=", mode)
                    switch(mode) {
                    case 0:
                        break;
                    case 1:
                    case 2:
                        NaWorkSessionsModel.resume(workSessionList.selectedItem)
                        break;
                    case 3:
                        NaWorkSessionsModel.pause(workSessionList.selectedItem)
                    }
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

            StyledButton {
                implicitHeight: 22 // Adjust as needed
                text: qsTr("To the Top")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.touch(workSessionList.selectedItem)
                }
            }

            Item {
                Layout.fillHeight: true
            }
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
