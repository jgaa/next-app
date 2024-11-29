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
            //enabled: root.enabled && (model.state == ReviewModel.State.READY || model.state == ReviewModel.State.DONE)
            hasReview: true
            onSelectionEvent: function (uuid) {
                actions.model.selectByUuid(uuid)
            }

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
                        visible: actions.model.state === ReviewModel.State.DONE || actions.model.state === ReviewModel.State.ERROR
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Start Over")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.restart()
                        }
                    }

                    StyledButton {
                        visible: actions.model.state === ReviewModel.State.READY
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
                        visible: actions.model.state === ReviewModel.State.READY
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Previous")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.previous()
                        }
                    }

                    StyledButton {
                        visible: actions.model.state === ReviewModel.State.READY
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Next")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.next()
                        }
                    }

                    StyledButton {
                        visible: actions.model.state === ReviewModel.State.READY
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Next List")
                        useWidth: actionCtl.buttonWidth
                        onClicked: {
                            actions.model.nextList()
                        }
                    }
                }
            }

            ProgressBar {
                id: progressCtl
                Layout.fillWidth: true
                value: actions.model.progress
                to: 100.0

                palette.dark: actions.model.state === ReviewModel.State.DONE ? "green" : "blue"
            }

            Rectangle {
                id: editActionCtl
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "white"
                enabled: root.enabled && (actions.model.state == ReviewModel.State.READY || actions.model.state == ReviewModel.State.DONE)

                // TODO: Synchronize data with the left view

                EditActionView {
                    id: editActionView
                    existingOnly: true
                    autoCommit: true
                    anchors.fill: parent
                }
            }
        }
    }
}
