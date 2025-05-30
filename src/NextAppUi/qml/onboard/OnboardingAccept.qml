import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models


ColumnLayout  {
    id: root
    anchors.fill: parent
    spacing: 20
    property bool accepted : false
    signal nextClicked()
    signal backClicked()

    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: greetingTextArea.implicitHeight
        Layout.alignment: Text.AlignHCenter

        TextArea {
            id: greetingTextArea
            //anchors.fill: parent
            textFormat: Text.RichText
            horizontalAlignment: Text.AlignHCenter
            font.pointSize: 12

            text: NaComm.signupInfo.greeting

            wrapMode: Text.WordWrap
            readOnly: true
            antialiasing: true
            color: MaterialDesignStyling.onSurface
            background: Rectangle {
                color: "transparent"
            }

            HoverHandler {
                enabled: parent.hoveredLink.length > 0
                cursorShape: Qt.PointingHandCursor
            }

            onLinkActivated: function(link) {
                Qt.openUrlExternally(link)
            }
        }
    }

    Item {
        Layout.preferredHeight: 20
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        TextArea {
            text: NaComm.signupInfo.eula
            textFormat: Text.RichText

            wrapMode: Text.WordWrap
            readOnly: true
            antialiasing: true
            color: "black"
            background: Rectangle {
                color: "white"
            }

            HoverHandler {
                enabled: parent.hoveredLink.length > 0
                cursorShape: Qt.PointingHandCursor
            }

            onLinkActivated: function(link) {
                Qt.openUrlExternally(link)
            }
        }
    }

    RowLayout {
        spacing: 4
        CheckBox {
            id: acceptCheckBox
            enabled: NaComm.signupInfo.eula.length > 0
            onCheckedChanged: {
                root.accepted = checked
            }
        }

        Text {
            text: qsTr("I accept the terms and conditions")
            color: MaterialDesignStyling.onSurface
        }
    }

    RowLayout {
        spacing: 20

        Layout.fillWidth: true
        Item {
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Back")
            onClicked: backClicked()
        }

        Button {
            text: qsTr("Next")
            enabled: accepted
            onClicked: {
                nextClicked()
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }

    Item {
        Layout.preferredHeight: 20
    }
}
