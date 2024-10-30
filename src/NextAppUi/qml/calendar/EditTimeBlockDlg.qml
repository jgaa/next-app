import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import Nextapp.Models

Dialog {
    id: root
    property NextappPb.timeBlock tb
    property int controlsPreferredWidth: 200

    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(500, NaCore.width, Screen.width)
    height: Math.min(600, NaCore.height, Screen.height)

    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        // console.log("Dialog opened :)");
        // console.log("TimeBlock id is", tb.id_proto)
        assign()
        actionsCtl.model = parent.model.getTimeBoxActionsModel(tb.id_proto, root)
    }

    function assign() {
        title.text = tb.name
        category.uuid = tb.category
        start.text = NaCore.toDateAndTime(tb.timeSpan.start, 0)
        end.text = NaCore.toDateAndTime(tb.timeSpan.end, tb.timeSpan.start)
    }

    function save() {
        tb.name = title.text
        tb.category = category.uuid
        tb.timeSpan.start = NaCore.parseDateOrTime(start.text, tb.timeSpan.start)
        tb.timeSpan.end = NaCore.parseDateOrTime(end.text, tb.timeSpan.start)
        tb.actions = actionsCtl.model.actions

        // TODO: Validate that from and to dates are the same!
        // TODO: Category
        // TODO: Actions / type

        parent.model.updateTimeBlock(tb)
    }

    ColumnLayout {
        anchors.fill: parent

        GridLayout {
            id: dlgfields
            Layout.fillWidth: true
            rowSpacing: 4
            columns: NaCore.isMobile ? 1 : 2

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Title")
            }

            DlgInputField {
                id: title
                Layout.fillWidth: true
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Category")
            }

            CategoryComboBox {
                Layout.fillWidth: true
                id: category
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Start")
            }

            DlgInputField {
                id: start
                Layout.preferredWidth: root.controlsPreferredWidth
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("End")
            }

            DlgInputField {
                id: end
                Layout.preferredWidth: root.controlsPreferredWidth
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Actions")
                visible: actionsCtl.visible
            }
        }

        ListView {
            id: actionsCtl
            Layout.fillHeight: true
            Layout.fillWidth: true
            visible: model !== null
            spacing: 4

            delegate: Rectangle {
                id: actionItem
                implicitHeight: 40
                implicitWidth: actionsCtl.width
                color: index % 2 ? MaterialDesignStyling.onPrimary : MaterialDesignStyling.primaryContainer
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property string category

                RowLayout {
                    height: actionItem.height
                    width: actionItem.width
                    id: actionItemLayout

                    Rectangle {
                        height: nameCtl.implicitHeight
                        width: 10
                        color: NaAcModel.valid ? NaAcModel.getColorFromUuid(category) : "transparent"
                        //radius: 5
                    }

                    Text {
                        font.family: ce.faNormalName
                        font.pointSize: nameCtl.font.pointSize
                        text: done ? "\uf058" : "\uf111"
                        color: done ? "green" : "orange"

                        Rectangle {
                            color: "white"
                            anchors.fill: parent
                            radius: 100
                            z: parent.z -1
                            visible: done
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        id: nameCtl
                        text: name
                        color: MaterialDesignStyling.onPrimaryContainer
                    }

                    RoundButton {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 16
                        Layout.preferredWidth: 150
                        icon.source:  "../../icons/fontawsome/trash-can.svg"
                        icon.color: "red"
                        onClicked: {
                            console.log("before remove: ", actionsCtl.model.actions)
                            actionsCtl.model.removeAction(root.tb.id_proto, uuid)
                            console.log("after remove: ", actionsCtl.model.actions)
                        }

                        text: qsTr("Remove")
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                z: parent.z - 1
                color: MaterialDesignStyling.primaryContainer
                radius: 5
            }
        }
    }

    onAccepted: {
        save()
        close()
    }

    onRejected: {
        close()
    }

    CommonElements {
        id: ce
    }
}
