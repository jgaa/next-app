import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models
import "common.js" as Common


Dialog {
    id: root
    title: qsTr("Your existing devices")

    padding: 10
    margins: 20
    modal: true
    x: NaCore.isMobile ? 0 : (parent.width - width) / 3
    y: NaCore.isMobile ? 0 : (parent.height - height) / 3
    width: NaCore.isMobile ? parent.width : Math.min(parent.width, 700)
    height: NaCore.isMobile ? parent.height - 10 : Math.min(parent.height - 10, 900)
    standardButtons: Dialog.Close

    Rectangle {
        anchors.fill: parent
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: MaterialDesignStyling.surface
    }

    Component.onCompleted: {
        devicesListCtl.model.refresh();
    }

    ColumnLayout {
        anchors.fill: parent

        StyledButton {
            text: qsTr("Get an OTP code for a new device")
            onClicked: {
                Common.openDialog("onboard/GetNewOtpForDevice.qml", root.parent, {});
            }
            // center vertically
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: MaterialDesignStyling.onPrimaryContainer
        }

        Label {
            text: qsTr("Your existing Devices")
            color: MaterialDesignStyling.onSecondaryContainer
        }

        ListView {
            id: devicesListCtl
            model: NaCore.getDevicesModel()
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // add some spacing between the rows
            spacing: 5

            ScrollBar.vertical: ScrollBar {
                id: vScrollBar
                parent: devicesListCtl
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: MaterialDesignStyling.scrollBarWidth
                policy: ScrollBar.AlwaysOn
            }

            delegate: Rectangle {
                width: devicesListCtl.width
                color: index % 2 ? MaterialDesignStyling.onPrimary : MaterialDesignStyling.primaryContainer
                height: content.implicitHeight
                radius: 5

                required property int index;
                required property string id;
                required property string name
                required property string user
                required property string created
                required property string hostName
                required property string os
                required property string osVersion
                required property string appVersion
                required property string productType
                required property string productVersion
                required property string arch
                required property string prettyName
                required property string lastSeen
                required property bool deviceEnabled
                required property string numSessions

                ColumnLayout {
                    id: content
                    anchors.fill: parent

                    GridLayout {
                        columns: 2
                        rowSpacing: 5

                        Label {
                            text: qsTr("Name")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }
                        Text {
                            text: name
                            font.bold: true
                            color: MaterialDesignStyling.onPrimaryContainer
                        }

                        Label {
                            text: qsTr("System")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }
                        Text{
                            text: prettyName
                            color: MaterialDesignStyling.onPrimaryContainer
                        }

                        Label {
                            text: qsTr("Last seen")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }
                        Text {
                            text: lastSeen
                            color: MaterialDesignStyling.onPrimaryContainer
                        }

                        Label {
                            text: qsTr("Sessions#")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }
                        Text {
                            text: numSessions
                            color: MaterialDesignStyling.onPrimaryContainer
                        }

                        Label {
                            text: qsTr("Created")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }
                        Text {
                            text: created
                            color: MaterialDesignStyling.onPrimaryContainer
                        }

                        Label {
                            text: qsTr("Enabled")
                            color: MaterialDesignStyling.onSecondaryContainer
                        }

                        RowLayout {
                            CheckBox {
                                id: myCheckBox
                                checked: deviceEnabled
                                enabled: NaComm.deviceId() != id // Dont allow us to disable the current device
                                onClicked: {
                                    devicesListCtl.model.enableDevice(id, checked)
                                }
                            }

                            CheckBoxWithFontIcon {
                                uncheckedCode: "\uf2ed"
                                uncheckedColor: "red"
                                autoToggle: false
                                visible: NaComm.deviceId() != id // Dont allow us to delete the current device

                                onClicked: {
                                    // Popup a dialog to confirm the deletion
                                    confirmDialog.deviceId = id
                                    confirmDialog.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    MessageDialog {
        id: confirmDialog
        property string deviceId;
        title: qsTr("Confirm Deletion")
        informativeText: qsTr("Are you sure you want to delete this device?")
        buttons: MessageDialog.Ok | MessageDialog.Cancel

        onAccepted: {
            console.log("User confirmed deletion!")
            devicesListCtl.model.deleteDevice(deviceId)
        }
    }
}
