import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    Layout.fillWidth: true

    property alias mode: modeCtl.currentIndex // 0 priority, 1 dynamic
    property alias priority: priorityCtl.currentIndex
    property alias urgency: urgencySlider.value
    property alias importance: importanceSlider.value
    readonly property bool valid: mode === 1 || (mode === 0 && priority >= 0)

    function reset() {
        mode = 0
        priority = -1
        urgency = 5
        importance = 5
    }

    signal priorityValueChanged(int priority)
    signal modeValueChanged(int mode)
    signal urgencyValueChanged(real urgency)
    signal importanceValueChanged(real importance)

    contentItem: ColumnLayout {
        id: layoutCtl
        spacing: 8

        ComboBox {
            id: modeCtl
            Layout.fillWidth: true
            model: ListModel {
                ListElement{ text: qsTr("Fixed Priority")}
                ListElement{ text: qsTr("Importance/Urgency")}
            }
            currentIndex: 0

            onCurrentIndexChanged: {
                // Dynamic actions have no fixed value yet. Allow switching modes and saving.
                if (currentIndex === 0 && root.priority < 0) {
                    root.priority = 4 // Normal
                }
                console.log("mode changed to", currentIndex)
                root.modeValueChanged(currentIndex)
            }
        }

        ComboBox {
            id: priorityCtl
            Layout.fillWidth: true
            visible: modeCtl.currentIndex === 0
            currentIndex: -1
            displayText: currentIndex === -1 ? qsTr("Priority") : currentText

            model: ListModel {
                ListElement{ text: qsTr("Critical")}
                ListElement{ text: qsTr("Very Important")}
                ListElement{ text: qsTr("Higher")}
                ListElement{ text: qsTr("High")}
                ListElement{ text: qsTr("Normal")}
                ListElement{ text: qsTr("Medium")}
                ListElement{ text: qsTr("Low")}
                ListElement{ text: qsTr("Insignificant")}
            }

            onCurrentIndexChanged: {
                console.log("priority changed to", currentIndex)
                if (currentIndex >= 0) {
                    root.priorityValueChanged(currentIndex)
                }
            }
        }

        ColumnLayout {
            id: slidersLayout
            Layout.fillWidth: true
            visible: modeCtl.currentIndex === 1
            spacing: 8

            Label { text: qsTr("Urgency") }

            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: urgencySlider
                    from: 0
                    to: 10
                    stepSize: 1
                    value: 5
                    snapMode: Slider.SnapAlways
                    Layout.fillWidth: true

                    onValueChanged: {
                        console.log("urgency changed to", value)
                        root.urgencyValueChanged(value)
                    }
                }
                Item {
                    Layout.preferredWidth: 8
                }
                Label {
                    text: urgencySlider.value
                }
            }

            SliderTicks {
                Layout.fillWidth: true   // important so that ticks match slider width
            }

            Label { text: qsTr("Importance") }

            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: importanceSlider
                    from: 0
                    to: 10
                    stepSize: 1
                    value: 5
                    snapMode: Slider.SnapAlways
                    Layout.fillWidth: true

                    onValueChanged: {
                        console.log("importance changed to", value)
                        root.importanceValueChanged(value)
                    }
                }
                Item {
                    Layout.preferredWidth: 8
                }
                Label {
                    text: importanceSlider.value
                }
            }

            SliderTicks {
                Layout.fillWidth: true   // important so that ticks match slider width
            }
        }
    }
}
