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
    property bool ready : false
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
            }
        }

        Label {
            text: qsTr("One Time Password")
            color: MaterialDesignStyling.onSurfaceVariant
        }

        TextField {
            id: otp
            Layout.fillWidth: true
            //color: MaterialDesignStyling.onSurface
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

    RowLayout {
        spacing: 20

        Layout.fillWidth: true
        Item {
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("Back")
            visible: NaComm.signupStatus !== NaComm.SIGNUP_SUCCESS
            onClicked: backClicked()
        }

        Button {
            id: createCtl
            text: qsTr("Add Device")
            enabled: otp.text.length == 8
            visible: NaComm.signupStatus !== NaComm.SIGNUP_SUCCESS
            onClicked: {
                loadingIndicator.visible = true;
                createCtl.enabled = false
                NaComm.addDeviceWithOtp(otp.text, email.text, deviceName.text)
            }
        }

        Button {
            id: nextCtl
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
}
