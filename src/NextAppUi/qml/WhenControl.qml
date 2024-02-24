import QtQuick
import QtQuick
import QtQuick.Controls
import NextAppUi
import nextapp.pb as NextappPB

Item {
    id: root
    property int when: 0
    property int dueType: NextappPB.ActionDueType.NONE
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
                    color: btn.textColor // Inherit text color from Button
                }
            }


        onClicked: {
            var ctl = Qt.createComponent("DueSelectionDialog.qml")
            if (ctl.status !== Component.Ready) {
                console.log("Error loading component:", ctl.errorString())
                return
            }
            var dialog = ctl.createObject(root, {
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

// Item {
//     id: root
//     property int when: 0
//     property int dueType: NextappPB.ActionDueType.NONE

//     width: button.width
//     height: button.height

//     onWhenChanged: {
//         if (when === 0) {
//             dueType = NextappPB.ActionDueType.NONE
//         }
//     }

//     ComboBox {
//         id: button
//         currentIndex: dueType
//         displayText: ActionsModel.formatWhen(when, dueType)

//         // TODO: Need more advanced model
//         model: ActionsModel.getDueSelections(when, dueType)
//         textRole: "display"

//         onCurrentIndexChanged: {
//             dueType = currentIndex
//             //displayText = ActionsModel.formatWhen(when, dueType)
//             switch(currentIndex) {
//             case NextappPB.ActionDueType.DATETIME:
//             case NextappPB.ActionDueType.DATE:
//             case NextappPB.ActionDueType.TIME:
//             case NextappPB.ActionDueType.WEEKDAY:
//             case NextappPB.ActionDueType.MONTHDAY:
//             case NextappPB.ActionDueType.YEAR:
//                 if (when == 0) {
//                     when = new Date().getTime() / 1000
//                 }
//                 break
//             case NextappPB.ActionDueType.NONE:
//                 when = 0
//                 break
//             }
//         }
//     }
// }
