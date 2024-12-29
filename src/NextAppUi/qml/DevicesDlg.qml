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
    title: qsTr("Existing devices")

    padding: 10
    margins: 20
    modal: true
    width: 300
    height: 600
    standardButtons: Dialog.Close

    Rectangle {
        anchors.fill: parent
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: MaterialDesignStyling.surface
    }

    ColumnLayout {
        anchors.fill: parent

        StyledButton {
            text: qsTr("Get OTP code for new device")
            onClicked: {
                //
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
                width: parent.width
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
                required property bool enabled

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

                        CheckBox {
                            //text: qsTr("Enabled")
                            checked: enabled
                        }
                    }
                }
            }
        }
    }
}
