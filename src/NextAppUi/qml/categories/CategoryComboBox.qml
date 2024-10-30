import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi
import nextapp.pb as NextappPB

ComboBox {
    id: root
    model: NaAcModel
    textRole: "name"
    property string uuid: ""
    currentIndex: -1
    displayText: currentIndex === -1 ? qsTr("Select a Category") : currentText

    onActivated: {
        var item = root.model.get(root.currentIndex)
        if (item) {
            root.uuid = item.id_proto
        }
    }

    onUuidChanged: {
        // console.log("CategoryComboBox/onUuidChanged: category is", root.uuid)
        if (root.uuid !== "") {
            root.currentIndex = root.model.getIndexByUuid(root.uuid)
        } else {
            root.currentIndex = -1
        }
    }

    delegate: ItemDelegate {
        width: root.width
        property var cat: root.model.get(index)
        highlighted: ListView.isCurrentItem
        text: cat.name
        background: Rectangle {
            color: index === root.currentIndex ? "lightsteelblue" : "transparent"

            Rectangle {
                x: parent.width - 20
                width: 20
                height: parent.height
                color: cat.color
            }
        }
    }

    contentItem: Item  {
        //color:  root.model.get(root.currentIndex).color
        Text {
            x: 10
            width: parent.width - 20
            height: parent.height
            text: root.displayText
            //horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            x: parent.width - 20
            width: 20
            height: parent.height
            color: root.model.get(root.currentIndex).color
            visible: root.currentIndex >= 0
        }
    }
}
