import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models
import nextapp.pb as NextappPb

Dialog {
    id: root
    x: NaCore.isMobile ? 0 : (ApplicationWindow.window.width - width) / 3
    y: NaCore.isMobile ? 0 : (ApplicationWindow.window.height - height) / 3
    width: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.width, 300) : 300
    height: ApplicationWindow.window !== null ? Math.min(ApplicationWindow.window.height - 10, 650) : 500
    title: qsTr("Database Info")
    property NextappPb.userDataInfo model: NaCore.dbInfo
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Make the grid scrollable
        ScrollView {
            id: scroller
            Layout.margins: 12
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // Keep content locked to the view's width so we only scroll vertically
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            GridLayout {
                id: grid
                width: scroller.availableWidth
                columns: width >= 400 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 8

                //Label { text: qsTr("Hash") }
                RowLayout {
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    spacing: 6

                    Label { text: qsTr("Hash") }

                    ToolButton {
                        id: hashInfo
                        text: "\u2139"             // ℹ info symbol
                        implicitWidth: 22
                        implicitHeight: 22
                        hoverEnabled: true
                        Accessible.name: qsTr("What is hash?")

                        // Nice for desktop + touch
                        ToolTip.delay: 0
                        ToolTip.timeout: 3000
                        ToolTip.visible: hovered || pressed
                        ToolTip.text: qsTr("A hash is a fingerprint of the database contents.")

                        onClicked: hashPopup.open()
                    }
                }

                TextArea {
                    text: root.model.hash
                    readOnly: true
                    wrapMode: TextArea.WrapAnywhere
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Nodes") }
                TextArea {
                    text: root.model.numNodes.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Action Categories") }
                TextArea {
                    text: root.model.numActionCategories.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Actions") }
                TextArea {
                    text: root.model.numActions.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Days") }
                TextArea {
                    text: root.model.numDays.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Work Sessions") }
                TextArea {
                    text: root.model.numWorkSessions.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Time Blocks") }
                TextArea {
                    text: root.model.numTimeBlocks.toString()
                    readOnly: true
                    Layout.fillWidth: true
                }
            }
        }
        // No spacer Item needed anymore; the ScrollView fills the space.
    }

    Popup {
        id: hashPopup
        modal: true
        focus: true
        padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(root.width - 20, 500)
        height: Math.min(root.height - 20, 600)
        x: NaCore.isMobile ? 0 : (root.width - width) / 6
        y: NaCore.isMobile ? 0 : (root.height - height) / 6

        background: Rectangle {
            radius: 6
            border.width: 1
            border.color: "#888"
            color: "#202020"           // Basic works fine with a dark bubble
        }

        contentItem: Label {
                // limit width so wrapping can occur (responsive to dialog width)
                width: Math.min(root.width - 48, 320)
                wrapMode: Text.WordWrap
                color: "white"
                text: qsTr("This is a content hash that uniquely identifies the current state of your database.\n"
                           + "It changes whenever the underlying data changes, so you can compare it across devices.\n"
                           + "After an import, the hash will always change because the identifiers of all database objects are replaced with new ones.")
        }

    }
}
