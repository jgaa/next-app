import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models
import "../common.js" as Common

//pragma ComponentBehavior:

Rectangle {
    id: root
    property alias model: listView.model
    property string selectedItem: ""
    property bool selectedIsActive: false
    color: MaterialDesignStyling.surface
    enabled: NaComm.connected

    DisabledDimmer {}

    function somethingChanged() {
        if (selectedItem !== "") {
            if(!model.sessionExists(selectedItem)) {
                selectedItem = ""
                selectedIsActive = false
            } else {
                selectedIsActive = WorkSessionsModel.isActive(selectedItem)
            }
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true

        Component.onCompleted: {
            // console.log("WorkSessionList.onCompleted")
            model.onModelReset.connect(function() {
                // console.log("WorkSessionList.onModelReset")
                if (root.selectedItem !== "" && !model.sessionExists(root.selectedItem)) {
                    // console.log("WorkSessionList.onModelReset clearing selectedItem")
                    root.selectedItem = ""
                }
                somethingChanged()
            })
        }

        delegate: Rectangle {
            id: delegateCtl
            implicitHeight: columnLayout.implicitHeight
            implicitWidth: parent.width

            required property int index
            required property string name
            required property string from
            required property string to
            required property string pause
            required property string duration
            required property string uuid
            required property bool active
            required property bool hasNotes
            required property string icon

            property bool selected : uuid == root.selectedItem
            //color: ListView.isCurrentItem ? MaterialDesignStyling.surfaceBright : MaterialDesignStyling.surface
            color: selected
                   ? MaterialDesignStyling.surfaceContainerHighest
                   : index % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer

            RowLayout {
                RowLayout {
                    SelectedIndicatorBar {
                        selected: delegateCtl.selected
                    }

                    // Icon
                    Text {
                        id: iconCtl
                        Layout.leftMargin: 4
                        text: icon
                        color: active ? "green" : "orange"
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 20
                        Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                ColumnLayout {
                    id: columnLayout
                    RowLayout {
                        implicitHeight: 20

                        Text {
                            id: nameCtl
                            color: MaterialDesignStyling.onSurface
                            text: name
                            font.bold: true

                            Component.onCompleted: {
                                font.pointSize *= 1.15;
                            }
                        }


                        Text {
                            id: notesIcon
                            Layout.margins: 4
                            visible: hasNotes
                            font.family: ce.faSolidName
                            font.styleName: ce.faSolidStyle
                            text: "\uf304"
                            color: MaterialDesignStyling.onSurfaceVariant
                        }
                    }

                    RowLayout {
                        implicitHeight: 20

                        Label {
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: qsTr("From: ")
                        }

                        Text {
                            color: MaterialDesignStyling.onSurface
                            text: from
                        }

                        Label {
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: qsTr("pause: ")
                        }

                        Text {
                            color: MaterialDesignStyling.onSurface
                            text: pause
                        }

                        Label {
                            color: MaterialDesignStyling.onSurfaceVariant
                            text: qsTr("duration: ")
                        }

                        Text {
                            color: MaterialDesignStyling.onSurface
                            text: duration
                        }
                    }

                    TapHandler {
                        onTapped: {
                            root.selectedItem = uuid
                            somethingChanged()
                        }

                        onLongPressed: (eventPoint, button) => {
                            contextMenu.uuid = uuid
                            contextMenu.name = qsTr("Edit Session")
                            contextMenu.popup();
                        }
                    }
                }
            }
        }
    }

    MessageDialog {
        id: confirmDelete

        property string uuid;
        property string name;

        title: qsTr("Do you really want to delete the Work Session \"%1\" ?").arg(name)
        text: qsTr("Note that any worked time, etc. for this session will also be deleted! This action can not be undone.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           WorkSessionsModel.deleteWork(uuid)
           confirmDelete.close()
        }

        onRejected: {
            confirmDelete.close()
        }
    }

    MyMenu {
        id: contextMenu
        property string uuid
        property string name

        Action {
            text: qsTr("Edit")
            icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/pen-to-square.svg"
            onTriggered: {
                openWorkSessionDlg(contextMenu.uuid)
            }
        }
        Action {
            icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/trash-can.svg"
            text: qsTr("Delete")
            onTriggered: {
                confirmDelete.uuid = contextMenu.uuid
                confirmDelete.name = contextMenu.name
                confirmDelete.open()
            }
        }
    }

    function openWorkSessionDlg(uuid) {
        Common.openDialog("qrc:/qt/qml/NextAppUi/qml/EditWorkSession.qml", root, {
            title: qsTr("Edit Work Session"),
            ws: listView.model.getSession(uuid),
            model: listView.model
        });
    }

    CommonElements {
        id: ce
    }
}
