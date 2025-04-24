import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import Nextapp.Models

Dialog {
    x: NaCore.isMobile ? 0 : (parent.width - width) / 3
    y: NaCore.isMobile ? 0 : (parent.height - height) / 3
    width: Math.min(parent.width, 800)
    height: Math.min(parent.height - 10, 600)
    title: qsTr("Notifications")

    standardButtons: Dialog.Close
    closePolicy: Dialog.CloseOnEscape
    modal: true

    ListView {
        id: listView
        anchors.fill: parent
        model: ModelInstances.getNotificationsModel()
        property int selectedRow: -1
        clip: true

        delegate: Rectangle {
            required property int index
            required property int id
            required property string subject
            required property string message
            required property string createdTime
            required property int kind
            property bool unread: id > listView.model.lastRead
            property var kinds: [qsTr("Info"), qsTr("Warning"), qsTr("Error"), qsTr("Upgrade"), qsTr("Outage"), qsTr("Promotion"), "deleted"]
            border.color: unread ? MaterialDesignStyling.primaryContainer :  MaterialDesignStyling.outlineVariant
            border.width: 1
            radius: 5
            Layout.topMargin: 5
            Layout.bottomMargin: 5

            width: listView.width
            height: contentCtl.implicitHeight
            color: index % 2 ? MaterialDesignStyling.surfaceContainerLowest : MaterialDesignStyling.surfaceContainerHighest

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onSingleTapped: {
                    listView.selectedRow = index;
                    if (id > listView.model.lastRead) {
                        listView.model.lastRead = id;
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                id: contentCtl

                SelectedIndicatorBar {
                    selected: listView.selectedRow == index;
                }

                ColumnLayout {
                    Layout.fillWidth : true
                    Layout.fillHeight: true
                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.preferredWidth: 20
                            text: "#" + id
                            color: unread ? MaterialDesignStyling.primary : MaterialDesignStyling.onSurfaceVariant
                        }

                        Item {
                            Layout.preferredWidth: 10
                        }

                        // Title
                        Text {
                            font.bold: true
                            color: unread ? MaterialDesignStyling.primary : MaterialDesignStyling.onSurface
                            text: subject
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: kinds[kind]
                        }

                        Label {
                            Layout.leftMargin: 10
                            text: createdTime
                            color: MaterialDesignStyling.onSurfaceVariant
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: message
                        wrapMode: Text.WordWrap
                        color: MaterialDesignStyling.onSurfaceVariant
                    }
                }
            }
        }
    }
}
