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

    TextArea {
        Layout.fillWidth: true
        Layout.fillHeight: true
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.RichText

        text: NaComm.signupInfo.greeting

        wrapMode: Text.WordWrap
        readOnly: true
        antialiasing: true
        color: MaterialDesignStyling.onSurface
        background: Rectangle {
            color: "transparent"
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
