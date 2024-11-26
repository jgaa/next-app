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
    //color: "green" // MaterialDesignStyling.surface
    enabled: NaComm.connected

    DisabledDimmer {}

    SplitView {
        id: splitCtl
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: SplitterStyle {}

        ActionsList {
            id: actions
            //color: "blue"
            SplitView.preferredWidth: parent.width / 2
            SplitView.minimumWidth: 100
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            model: NaCore.getReviewModel()
            enabled: root.enabled && model.state == ReviewModel.State.READY

            onVisibleChanged: {
                if (visible) {
                    actions.model.active = visible
                }
            }
        }

        ColumnLayout {
            SplitView.preferredWidth: parent.width / 2
            SplitView.minimumWidth: 100
            SplitView.fillWidth: true
            SplitView.fillHeight: true

            ToolBar {
                Layout.fillWidth: true
                background: Rectangle {
                    color: MaterialDesignStyling.surfaceDim
                }

                RowLayout {
                    //spacing: 10
                    Layout.fillWidth: true
                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Back")
                        onClicked: {
                            //actions.pop()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Previous")
                        onClicked: {
                            //actions.pop()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Next")
                        onClicked: {
                            //actions.pop()
                        }
                    }
                }
            }

            Rectangle {
                id: actionCtl
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "white"
                enabled: actions.enabled

                // TODO: Synchronize data with the left view

                EditActionView {
                    id: editActionView
                    anchors.fill: parent
                }
            }
        }
    }
}
