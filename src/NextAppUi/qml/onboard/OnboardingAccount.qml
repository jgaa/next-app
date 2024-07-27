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

    Settings {
        id: settings
        property string userName: ""
        property string userEmail: ""
        property string companyName: ""
        property string deviceName: ""
    }

    GridLayout {
        columns: NaCore.isMobile ? 1 : 2
        Layout.margins: 20

        Label {
            text: qsTr("Your Name")
            color: MaterialDesignStyling.onSurfaceVariant
        }

        TextField {
            id: name
            Layout.fillWidth: true
            text: settings.userName
            //color: MaterialDesignStyling.onSurface
            onTextChanged: {
                settings.userName = name.text
                validate()
            }
        }

        Label {
            text: qsTr("Company Name (leave blank if none)")
            color: MaterialDesignStyling.onSurfaceVariant
        }

        TextField {
            id: company
            Layout.fillWidth: true
            text: settings.companyName
            //color: MaterialDesignStyling.onSurface
            onTextChanged: {
                settings.companyName = name.text
                validate()
            }
        }

        Label {
            text: qsTr("This device's name (so you can identify it later)")
            color: MaterialDesignStyling.onSurfaceVariant
        }

        TextField {
            id: deviceName
            Layout.fillWidth: true
            text: settings.deviceName
            //color: MaterialDesignStyling.onSurface
            onTextChanged: {
                settings.deviceName = deviceName.text
                validate()
            }
        }


        Label {
            text: qsTr("Email")
            color: MaterialDesignStyling.onSurfaceVariant
        }

        TextField {
            id: email
            Layout.fillWidth: true
            text: settings.userEmail
            //color: MaterialDesignStyling.onSurface
            onTextChanged: {
                settings.userEmail = email.text
                validate()
            }
        }
    }

    RowLayout {
        spacing: 20
        visible: NaComm.signupStatus === NaComm.SIGNUP_SIGNING_UP

        Item {Layout.fillWidth: true }

        Rectangle {
            id: loadingIndicator
            width: 50
            height: 50
            radius: 20
            color: "transparent"
            border.color: "green"
            border.width: 3

            RotationAnimation {
                id: spinAnimation
                target: loadingIndicator
                property: "rotation"
                from: 0
                to: 360
                duration: 2000
                running: loadingIndicator.visible
                loops: Animation.Infinite
            }
        }

        Item {Layout.fillWidth: true }
    }

    ScrollView {
        Layout.leftMargin: 20
        Layout.rightMargin: 20
        Layout.preferredHeight: 200
        Layout.fillWidth: true

        Text {
            text: NaComm.messages
            wrapMode: Text.Wrap
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
            id: createCtl
            text: qsTr("Create Account")
            enabled: accepted
            visible: NaComm.signupStatus !== NaComm.SIGNUP_SUCCESS
            onClicked: {
                //nextClicked()
                loadingIndicator.visible = true;
                createCtl.enabled = false
                NaComm.signup(name.text, email.text, company.text, deviceName.text)
            }
        }

        Button {
            visible: !createCtl.visible
            text: qsTr("Next")
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

    function validate() {
        accepted = name.text.length >= 3 && isValidEmail(email.text)
    }

    function isValidEmail(email) {
        const emailPattern = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/;
        return emailPattern.test(email);
    }
}
