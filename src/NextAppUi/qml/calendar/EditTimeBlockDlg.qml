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
    width: 500
    height: 500

    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        console.log("Dialog opened :)");
        console.log("TimeBlock id is", tb.id_proto)
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
            columns: 2

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Title")
            }

            DlgInputField {
                id: title
                Layout.preferredWidth: root.controlsPreferredWidth * 2
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Category")
            }

            CategoryComboBox {
                id: category
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("Start")
            }

            DlgInputField {
                id: start
            }

            Label {
                Layout.alignment: Qt.AlignLeft
                color: Colors.disabledText
                text: qsTr("End")
            }

            DlgInputField {
                id: end
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
            Layout.preferredWidth: root.controlsPreferredWidth * 2
            interactive: false
            visible: model !== null
            spacing: 4

            delegate: Rectangle {
                id: actionItem
                implicitHeight: actionItemLayout.implicitHeight
                implicitWidth: actionsCtl.width
                color: index % 2 ? MaterialDesignStyling.onPrimary : MaterialDesignStyling.primaryContainer
                required property int index
                required property string name
                required property string uuid
                required property bool done
                required property string category

                RowLayout {
                    width: actionsCtl.width
                    id: actionItemLayout

                    Rectangle {
                        height: nameCtl.implicitHeight
                        width: 10
                        color: ActionCategoriesModel.valid ? ActionCategoriesModel.getColorFromUuid(category) : "transparent"
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

                    Button {
                        Layout.preferredHeight: 20
                        Layout.preferredWidth: 100
                        icon.source:  "../../icons/fontawsome/trash-can.svg"
                        icon.color: "red"
                        onClicked: {
                            actionsCtl.model.removeAction(root.tb.id_proto, uuid)
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
