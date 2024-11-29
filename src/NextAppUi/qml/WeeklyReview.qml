import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Rectangle {
    id: root
    //color: "green" // MaterialDesignStyling.surface
    enabled: NaComm.connected
    property var navigation: null
    property bool started: false
    color: MaterialDesignStyling.surface

    DisabledDimmer {}

    // The layout for the initial view
    ColumnLayout {
        id: startScreen
        anchors.centerIn: parent
        visible: !started // Only show this screen when started is false
        spacing: 20

        Text {
            text: "Welcome to the Weekly Review Workflow."
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            color: MaterialDesignStyling.onSurface
        }

        Text {
            text: qsTr("The Weekly Review is a cornerstone of the GTD (Getting Things Done) methodology, designed to help you reflect, organize, and prioritize your tasks and projects. It ensures you stay aligned with your goals by reviewing current commitments, and upcoming plans. \r\n\r\nStarting the review workflow in the app will guide you step-by-step through all your lists and actions for clarity and focus.")
            wrapMode: Text.WordWrap
            Layout.preferredWidth: parent.width * 0.9
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            color: MaterialDesignStyling.onSurface
            clip: true
        }

        StyledButton {
            text: qsTr("Start your Weekly Review")
            Layout.alignment: Qt.AlignHCenter
            onClicked: {
                root.started = true; // Switch to WeeklyReviewView
            }
        }
    }

    Loader {
        id: weeklyReviewLoader
        anchors.fill: parent
        visible: root.started // Show WeeklyReviewView only when started is true
        sourceComponent: WeeklyReviewView {
            anchors.fill: parent
            navigation: root.navigation
        }
    }


}
