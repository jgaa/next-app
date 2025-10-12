// FeedbackDialog.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import NextAppUi
import Nextapp.Models
import nextapp.pb
//import "common.js" as Common

Dialog {
    id: root
    focus: true
    padding: 10
    margins: 20
    modal: true
    x: NaCore.isMobile ? 0 : (parent.width - width) / 3
    y: NaCore.isMobile ? 0 : (parent.height - height) / 3
    width: NaCore.isMobile ? parent.width : Math.min(parent.width, 450)
    height: NaCore.isMobile ? parent.height - 10 : Math.min(parent.height - 10, 600)

    title: qsTr("Send Feedback")
    property string emojiFont: (Qt.platform.os === "linux")  ? "Noto Color Emoji"
                             : (Qt.platform.os === "windows")? "Segoe UI Emoji"
                             : /* macOS/iOS */                 "Apple Color Emoji"
    standardButtons: Dialog.Cancel | Dialog.Ok

    onOpened: {
        cbKind.currentIndex = Feedback.Kind.OTHER
        cbEmoji.currentIndex = Feedback.Emoji.NEUTRAL
        messageArea.text = ""
        answerSwitch.checked = false
        logSwitch.checked = false
    }

    // Internal model for the outgoing protobuf value
    property feedback fm

    // ComboBox models with enum values
    readonly property var kindModel: [
        { text: qsTr("Bug"),      value: Feedback.Kind.BUG },
        { text: qsTr("Idea"),     value: Feedback.Kind.IDEA },
        { text: qsTr("Thoughts"), value: Feedback.Kind.THOUGHTS },
        { text: qsTr("Other"),    value: Feedback.Kind.OTHER }
    ]

    readonly property var emojiModel: [
        { emoji: "😊", label: qsTr("Smile"),   value: Feedback.Emoji.SMILE },
        { emoji: "☹️", label: qsTr("Frown"),   value: Feedback.Emoji.FROWN },
        { emoji: "😐", label: qsTr("Neutral"), value: Feedback.Emoji.NEUTRAL },
        { emoji: "😠", label: qsTr("Angry"),   value: Feedback.Emoji.ANGRY },
        { emoji: "😍", label: qsTr("Love"),    value: Feedback.Emoji.LOVE },
        { emoji: "😢", label: qsTr("Sad"),     value: Feedback.Emoji.SAD }
    ]

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: qsTr("Your thoughts help us improve.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true

                Label { text: qsTr("Type"); font.bold: true }
                ComboBox {
                    id: cbKind
                    Layout.fillWidth: true
                    textRole: "text"
                    model: root.kindModel
                    currentIndex: 0
                    Accessible.name: qsTr("Feedback type")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true

                Label { text: qsTr("Emoji"); font.bold: true }
                ComboBox {
                    id: cbEmoji
                    Layout.fillWidth: true
                    model: root.emojiModel
                    textRole: "emoji"

                    delegate: ItemDelegate {
                        required property var modelData
                        width: cbEmoji.width
                        contentItem: Row {
                            spacing: 8
                            Text { text: modelData.emoji; font.family: root.emojiFont; font.pixelSize: 20 }
                            Text { text: modelData.label }
                        }
                        Accessible.name: modelData.label
                        highlighted: ListView.isCurrentItem
                        onClicked: cbEmoji.currentIndex = index
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: (cbEmoji.currentIndex >= 0)
                                  ? root.emojiModel[cbEmoji.currentIndex].label
                                  : ""
                    font.family: root.emojiFont
                    font.pixelSize: 22
                }
            }
        }

        Label { text: qsTr("Message"); font.bold: true }

        ScrollView {
            Layout.fillHeight: true
            Layout.fillWidth: true

            //ScrollBar.horizontal.policy: ScrollBar.AlwaysOn
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn
            TextArea {
                id: messageArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: qsTr("Describe the issue or share your idea…")
                wrapMode: TextArea.Wrap
                clip: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Switch {
                id: answerSwitch
                Accessible.name: qsTr("Request an answer")
            }
            Label {
                text: qsTr("Request an answer (on email)")
                Layout.fillWidth: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Switch {
                id: logSwitch
                Accessible.name: qsTr("Attach the current log")
            }
            Label {
                text: qsTr("Attach the current log")
                Layout.fillWidth: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

    }

    // Build and send the protobuf when user presses Send/OK
    onAccepted: {
        // Fill the value-type fields
        fm.kind = root.kindModel[cbKind.currentIndex].value
        fm.emoji = root.emojiModel[cbEmoji.currentIndex].value
        fm.hasLog = logSwitch.checked
        fm.message = messageArea.text
        fm.requestsAnswer = answerSwitch.checked

        NaComm.sendFeedback(fm)

        // Optional: reset UI
        cbKind.currentIndex = 0
        cbEmoji.currentIndex = 0
        messageArea.text = ""
        answerSwitch.checked = false
    }
}
