import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models

Window  {
    id: root
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(800, NaCore.height, Screen.height)
    visible: true
    property bool isNewUser: false
    property bool onboardingComplete: false
    signal onboardingDone()

    onClosing: function(event) {
       if (!root.onboardingComplete) {
           // user closed early → quit the entire application
           Qt.quit()
       }
   }

    Rectangle {
        anchors.fill: parent
        color: MaterialDesignStyling.surface
        border.color: MaterialDesignStyling.outline

        ColumnLayout {
            anchors.fill: parent

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: MaterialDesignStyling.surface
                border.color: MaterialDesignStyling.outline

                Settings {
                    id: settings
                    property bool onboarding: false
                }

                StackView {
                    id: stackView
                    anchors.fill: parent
                    initialItem: onboardingPage1
                }

                Component {
                    id: onboardingPage1
                    OnboardingWelcome {
                        onNextClicked: {
                            stackView.push(onboardingPage2)
                        }
                    }
                }

                Component {
                    id: addDevicePage1
                    AddDevicePage {
                        onNextClicked: {
                            root.isNewUser = false
                            stackView.push(onboardingPage5)
                        }
                        onBackClicked: stackView.pop()
                    }
                }

                Component {
                    id: onboardingPage2
                    OnboardingServer {
                        onNextNewSubscrClicked: stackView.push(onboardingPage3)
                        onNextAddDeviceClicked: stackView.push(addDevicePage1)
                        onBackClicked: stackView.pop()
                    }
                }

                Component {
                    id: onboardingPage3
                    OnboardingAccept {
                        onNextClicked: stackView.push(onboardingPage4)
                        onBackClicked: stackView.pop()
                    }
                }

                Component {
                    id: onboardingPage4
                    OnboardingAccount {
                        onNextClicked: {
                            root.isNewUser = true
                            stackView.push(onboardingPage5)
                        }
                        onBackClicked: stackView.pop()
                    }
                }

                Component {
                    id: onboardingPage5
                    OnboardingReady {
                        id: lastPageContent
                        newUser: root.isNewUser
                        onNextClicked: {
                            onboardingDone()
                            // Switch to the main UI
                            root.onboardingComplete = true
                            stackView.clear()
                            NaComm.signupDone();
                            console.log("Closing window")
                            root.close()
                            root.destroy()
                        }
                    }
                }
            }

            ScrollView {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.preferredHeight: 130
                Layout.fillWidth: true

                Text {
                    text: NaComm.messages
                    wrapMode: Text.Wrap
                    color: MaterialDesignStyling.onSurface
                }
            }
        } // ColumnLayout
    }
}
