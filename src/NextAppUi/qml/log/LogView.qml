import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import Nextapp.Models


Rectangle {
    id: root
    color: "white"
    ListView {
        anchors.fill: parent
        model: NaLogModel
        clip: true

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOn
        }

        delegate: Rectangle {
            id: item
            width: root.width
            height: content.implicitHeight
            color: index % 2 ? "white" : "azure"

            required property int index
            required property string time
            required property string message
            required property string severity
            required property string eventColor

            ColumnLayout {
                id: content
                anchors.fill: parent
                RowLayout {
                    spacing: 5
                    Text {
                        id: timeCtl
                        text: time
                        color: eventColor
                        Layout.alignment: Qt.AlignTop
                    }

                    Text {
                        id: severityCtl
                        text: severity
                        color: eventColor
                        Layout.alignment: Qt.AlignTop
                    }

                    Text {
                        //width: item.width - 10 - timeCtl.width - severityCtl.width
                        wrapMode: Text.Wrap
                        text: message
                        color: eventColor
                        Layout.fillWidth: true
                        visible: !NaCore.isMobile
                    }
                }
                Text {
                    id: mobileText
                    //width: item.width - 10 - timeCtl.width - severityCtl.width
                    wrapMode: Text.Wrap
                    text: message
                    color: eventColor
                    Layout.fillWidth: true
                    visible: NaCore.isMobile
                    Layout.leftMargin: 12
                    Layout.bottomMargin: 12
                }
            }
        }
    }
}
