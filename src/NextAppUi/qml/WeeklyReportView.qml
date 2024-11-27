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
    property var navigation: null

    DisabledDimmer {}

    Connections {
        target: actions.model

        function onSelectedChanged() {
            if (actions.listCtl.currentIndex !=- actions.model.selected) {
                console.log("WeeklyReportView: Selected changed to ", actions.model.selected)
                actions.listCtl.currentIndex = actions.model.selected
                editActionCtl.enabled = false
            }
        }

        // function onModelReset() {
        //     navigation.setUuidAsCurrent(actions.model.currentUuid)
        // }

        function onNodeUuidChanged() {
            root.navigation.setUuidAsCurrent(actions.model.nodeUuid)
        }

        function onActionChanged() {
            console.log("WeeklyReportView: Action changed to: ", actions.model.action.name)
            editActionView.assign(actions.model.action)
            editActionCtl.enabled = actions.model.actionUuid !== "";
        }
    }

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
                    if (navigation) {
                        navigation.setUuidAsCurrent(actions.model.currentUuid)
                    }
                }
            }
        }

        ColumnLayout {
            id: actionCtl
            property int buttonWidth: width / 6
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
                        text: qsTr("First")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.first()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Back")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.back()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Previous")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.previous()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Next")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.next()
                        }
                    }

                    StyledButton {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Next List")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.next()
                        }
                    }
                }
            }

            Rectangle {
                id: editActionCtl
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "white"
                enabled: false

                // TODO: Synchronize data with the left view

                EditActionView {
                    id: editActionView
                    anchors.fill: parent
                }
            }
        }
    }
}
