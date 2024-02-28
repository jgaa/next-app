import QtQuick
import QtQuick.Controls
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as Common

Item {
    id: root
    property NextappPB.due due
    height: btn.height

    signal selectionChanged(NextappPB.due due)

    Button {
        id: btn
        //text: ActionsModel.formatWhen(when, dueType)
        icon.source: "../icons/fontawsome/calendar.svg"
        icon.color: "blue"
        width: parent.width

        contentItem:
            Row {
                spacing: 10
                Image {
                    source: "../icons/fontawsome/calendar.svg"
                    sourceSize.width: 20
                    sourceSize.height: 20
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: ActionsModel.formatDue(root.due)
                    verticalAlignment: Text.AlignVCenter
                    font: btn.font // Inherit font from Button
                }
            }


        onClicked: {
            var dialog = Common.createDialog("DueSelectionDialog.qml", root, {
                due: root.due,
            })

            if (dialog !== null) {
                dialog.selectionChanged.connect(function(due) {
                    // Relay the signal to notify our parent
                    root.due = due
                    selectionChanged(due)
                })
            }

            dialog.open()
        }
    }

    // DueSelectionDialog {
    //     id: dialog
    //     due: root.due
    //     onSelectionChanged: {
    //         root.due = due
    //         selectionChanged(due)
    //     }
    // }
}

