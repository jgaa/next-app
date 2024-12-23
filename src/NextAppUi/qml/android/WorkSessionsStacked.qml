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

    DisabledDimmer {}

    GridLayout {
        columns: 1
        anchors.fill: parent

        WorkSessionsCompactList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            id: workSessionList
            model: NaWorkSessionsModel
        }

        GridLayout {
            RoundButton {
                Layout.leftMargin: 10
                text: workSessionList.selectedIsActive ? qsTr("Pause") : qsTr("Resume")
                enabled: workSessionList.selectedItem !== ""
                onClicked: {
                    if (workSessionList.selectedIsActive) {
                        NaWorkSessionsModel.pause(workSessionList.selectedItem)
                    } else {
                        NaWorkSessionsModel.resume(workSessionList.selectedItem)
                    }
                }
            }

            RoundButton {
                Layout.leftMargin: 10
                text: qsTr("Done")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.done(workSessionList.selectedItem)
                }
            }

            RoundButton {
                Layout.leftMargin: 10
                text: qsTr("Action Done")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.finishAction(workSessionList.selectedItem)
                }
            }

            RoundButton {
                Layout.leftMargin: 10
                text: qsTr("To the Top")
                enabled: workSessionList.selectedItem !== ""

                onClicked: {
                    NaWorkSessionsModel.touch(workSessionList.selectedItem)
                }
            }
        }
    }
}
