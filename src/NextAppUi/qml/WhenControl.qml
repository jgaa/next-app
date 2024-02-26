import QtQuick
import QtQuick
import QtQuick.Controls
import NextAppUi
import nextapp.pb as NextappPB
import "common.js" as Common

Item {
    id: root
    property int when: 0
    property int dueType: NextappPB.ActionDueKind.NONE
    height: btn.height

    signal selectionChanged(int when, int dueType)

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
                    text: ActionsModel.formatWhen(when, dueType)
                    verticalAlignment: Text.AlignVCenter
                    font: btn.font // Inherit font from Button
                    //color: btn.textColor // Inherit text color from Button
                }
            }


        onClicked: {
            var dialog = Common.createDialog("DueSelectionDialog.qml", root, {
                when: root.when,
                dueType: root.dueType
            })

            if (dialog !== null) {
                dialog.selectionChanged.connect(function(when, dueType) {
                    // Relay the signal to notify our parent
                    root.when = when
                    root.dueType = dueType
                    selectionChanged(when, dueType)
                })
            }

            dialog.open()
        }
    }
}

