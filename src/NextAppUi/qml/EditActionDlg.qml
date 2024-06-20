import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import "common.js" as Common
import Nextapp.Models

Dialog {
    id: root
    property string node: NaMainTreeModel.selected
    property ActionPrx aprx
    property NextappPb.action action: aprx.action
    property bool assigned: false
    property bool valid: aprx.valid
    property int controlsPreferredWidth: 200

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: 800
    height: 600

    standardButtons: root.aprx.valid ? (Dialog.Ok | Dialog.Cancel) : Dialog.Cancel

    onOpened: {
        // console.log("Dialog opened :)");
        // console.log("action.name is", action.name)
        assign()
    }

    onValidChanged: {
        if (valid) {
            assign()
        } else {
            // TODO: Popup
            // console.log("Failed to fetch existing Action")
        }
    }

    function assign() {
        if (aprx.valid && !root.assigned) {

            //root.action = aprx.action

            // Set the values in the controld. We can't bind them directly for some reason.
            status.currentIndex = root.action.status
            name.text = root.action.name = action.name
            descr.text = root.action.descr
            priority.currentIndex = root.action.priority
            createdDateCtl.text = Common.formatPbDate(root.action.createdDate)
            timeEstimateCtl.text = Common.minutesToText(root.action.timeEstimate)
            difficultyCtl.currentIndex = root.action.difficulty
            repeatWhenCtl.currentIndex = root.action.repeatWhen
            repeatUnitCtl.currentIndex = root.action.repeatUnits
            repeatKindCtl.currentIndex = root.action.repeatKind
            favorite.isChecked = root.action.favorite
            category.uuid = root.action.category

            // console.log("EditActionDlg/assign category=", root.action.category)

            if (root.action.repeatWhen === 0 /* AT_DATE */) {
                repeatAfterCtl.value = root.action.repeatAfter
            } else {
                updateListFromInt(repeatSpecCtl.model, root.action.repeatAfter)
            }

            if (root.action.completedTime !== 0) {
                completedTimeCtl.text = Common.formatPbTimeFromTimet(root.action.completedTime)
            } else {
                completedTimeCtl.text = qsTr("Not completed yet")
            }

            if (action.node === "") {
                action.node = node;
            }

            if (action.node === "") {
                throw "No node"
            }

            // Don't do it again for this instance
            root.assigned = true
        }
    }

    ColumnLayout {
        visible: root.aprx.valid
        anchors.fill: parent

        TabBar {
            id: bar
            Layout.fillWidth: true

            TabButton {
                Layout.preferredWidth: 100
                text: qsTr("What/When")
            }

            TabButton {
                Layout.preferredWidth: 100
                text: qsTr("Details")
            }

            TabButton {
                Layout.preferredWidth: 100
                text: qsTr("Repeat")
            }
        }

        StackLayout {
            width: parent.width
            currentIndex: bar.currentIndex

            // Main tab
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                GridLayout {
                    id: dlgfields
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    rowSpacing: 4
                    columns: 2

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Name")
                    }

                    DlgInputField {
                        id: name
                        Layout.preferredWidth: root.controlsPreferredWidth * 3
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Status")
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignLeft
                        ComboBox {
                            id: status
                            Layout.preferredWidth: root.controlsPreferredWidth
                            model: ListModel {
                                ListElement{ text: qsTr("Active")}
                                ListElement{ text: qsTr("Done")}
                                ListElement{ text: qsTr("On Hold")}
                            }
                        }

                        CategoryComboBox {
                            id: category
                            Layout.preferredWidth: root.controlsPreferredWidth
                        }

                        CheckBoxWithFontIcon {
                            id: favorite
                            Layout.preferredWidth: root.controlsPreferredWidth
                            checkedCode: "\uf005"
                            uncheckedCode: "\uf005"
                            checkedColor: "orange"
                            uncheckedColor: "lightgray"
                            useSolidForChecked: true
                            text: qsTr("Favorite")
                        }

                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("When")
                    }

                    RowLayout {
                        WhenControl {
                            //width: root.controlsPreferredWidth * 2
                            id: whenControl
                            due: root.action.due
                            Layout.preferredWidth: root.controlsPreferredWidth

                            onSelectionChanged: {
                                // console.log("DueType changed to", whenControl.due.kind)
                                //root.action.due = whenControl.due
                                shortcuts.currentIndex = -1
                            }
                        }
                        ComboBox {
                            id: shortcuts
                            Layout.preferredWidth: root.controlsPreferredWidth

                            displayText: qsTr("Move the due time")
                            currentIndex: -1
                            model: ListModel {
                                ListElement{ text: qsTr("Today")}
                                ListElement{ text: qsTr("Tomorrow")}
                                ListElement{ text: qsTr("This Weekend")}
                                ListElement{ text: qsTr("Next Monday")}
                                ListElement{ text: qsTr("This week")}
                                ListElement{ text: qsTr("After one week")}
                                ListElement{ text: qsTr("Next Week")}
                                ListElement{ text: qsTr("This month")}
                                ListElement{ text: qsTr("Next month")}
                                ListElement{ text: qsTr("This Quarter")}
                                ListElement{ text: qsTr("Next Quarter")}
                                ListElement{ text: qsTr("This Year")}
                                ListElement{ text: qsTr("Next Year")}
                            }

                            onCurrentIndexChanged: {
                                if (currentIndex >= 0) {
                                    whenControl.due = ActionsModel.changeDue(currentIndex, whenControl.due)
                                }
                            }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Priority")
                    }

                    ComboBox {
                        id: priority
                        Layout.preferredWidth: root.controlsPreferredWidth
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
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Description")
                    }

                    TextArea {
                        id: descr
                        //Layout.preferredHeight: 200
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.controlsPreferredWidth * 3
                        placeholderText: qsTr("Some words to describe the purpose of this item?")
                        //text: root.action.descr

                        background: Rectangle {
                            color: descr.focus ? "lightblue" : "lightgray"
                        }
                    }
                }
            } // Main tab

            // Details tab
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                GridLayout {
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    rowSpacing: 4
                    columns: 2


                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Created")
                    }

                    Text {
                        id: createdDateCtl
                        color: Colors.disabledText
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Completed")
                    }

                    Text {
                        id: completedTimeCtl
                        color: Colors.disabledText
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Time Estimate")
                    }

                    RowLayout {
                        DlgInputField {
                            id: timeEstimateCtl
                            //inputMask: "999:99:99"
                            Layout.preferredWidth: 60
                            //text: root.action.name
                        }
                        Text {
                            leftPadding: 10
                            text: qsTr("[[ days: ] hours: ] minutes  day=8h")
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Difficulty")
                    }

                    ComboBox {
                        id: difficultyCtl
                        Layout.preferredWidth: root.controlsPreferredWidth
                        model: ListModel {
                            ListElement{ text: qsTr("Trivial")}
                            ListElement{ text: qsTr("Easy")}
                            ListElement{ text: qsTr("Normal")}
                            ListElement{ text: qsTr("Hard")}
                            ListElement{ text: qsTr("Very Hard")}
                            ListElement{ text: qsTr("Inspiered moment")}
                        }
                    }
                }
            } // Details tab

            // Repeat tab
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                GridLayout {
                    id: grid

                    Layout.alignment: Qt.AlignLeft
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    rowSpacing: 4
                    columns: 2

                    property bool showControls: repeatKindCtl.currentIndex > 0

                    property bool showRepeatAfterCtl :
                        showControls && repeatWhenCtl.currentIndex === 0

                    property bool showRepeatSpecCtl :
                        showControls && repeatWhenCtl.currentIndex === 1

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Repeat?")
                    }

                    ComboBox {
                        id: repeatKindCtl
                        Layout.preferredWidth: root.controlsPreferredWidth
                        currentIndex: 0
                        model: ListModel {
                            ListElement{ text: qsTr("Never")}
                            ListElement{ text: qsTr("From Completed time")}
                            ListElement{ text: qsTr("From Start time")}
                            ListElement{ text: qsTr("From Due time")}
                        }
                    }

                    Label {
                        visible: grid.showControls
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("How")
                    }

                    ComboBox {
                        id: repeatWhenCtl
                        visible: grid.showControls
                        Layout.preferredWidth: root.controlsPreferredWidth
                        currentIndex: -1
                        model: ListModel {
                            ListElement{ text: qsTr("At Date")}
                            ListElement{ text: qsTr("At Specific day")}
                        }
                    }

                    Label {
                        visible: grid.showRepeatAfterCtl
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("After")
                    }

                    RowLayout {
                        visible: grid.showRepeatAfterCtl
                        SpinBox {
                            id: repeatAfterCtl
                            editable: true
                            from: 1
                            //Layout.preferredWidth: 30
                        }
                        ComboBox {
                            id: repeatUnitCtl
                            Layout.leftMargin: 10
                            //Layout.preferredWidth: root.controlsPreferredWidth
                            model: ListModel {
                                ListElement{ text: qsTr("Days")}
                                ListElement{ text: qsTr("Weeks")}
                                ListElement{ text: qsTr("Months")}
                                ListElement{ text: qsTr("Quarters")}
                                ListElement{ text: qsTr("Years")}
                            }
                        }
                    }

                    Item {
                        visible: grid.showRepeatSpecCtl
                    }

                    ListView {
                        id: repeatSpecCtl
                        Layout.preferredWidth: root.controlsPreferredWidth
                        Layout.fillHeight: true
                        clip: true
                        visible: grid.showRepeatSpecCtl
                        model: ListModel {
                            ListElement{ text: qsTr("Sunday"); checked: false }
                            ListElement{ text: qsTr("Monday"); checked: false }
                            ListElement{ text: qsTr("Tuesday"); checked: false }
                            ListElement{ text: qsTr("Wednesday"); checked: false }
                            ListElement{ text: qsTr("Thursday"); checked: false }
                            ListElement{ text: qsTr("Friday"); checked: false }
                            ListElement{ text: qsTr("Saturday"); checked: false }
                            ListElement{ text: qsTr("First Day in Week"); checked: false }
                            ListElement{ text: qsTr("Last Day of Week"); checked: false }
                            ListElement{ text: qsTr("First Day in Month"); checked: false }
                            ListElement{ text: qsTr("Last Day in Month"); checked: false }
                            ListElement{ text: qsTr("First Day in Quarter"); checked: false }
                            ListElement{ text: qsTr("Last Day in Quarter"); checked: false }
                            ListElement{ text: qsTr("First Day in Year"); checked: false }
                            ListElement{ text: qsTr("Last Day in Year"); checked: false }
                        }
                        delegate: Item {
                            Layout.fillWidth: true
                            height: checkBox.height
                            CheckBox {
                                id: checkBox
                                checked: model.checked
                                text: model.text
                                //onCheckedChanged: model.checked = checked
                                onClicked: model.checked = checked
                            }
                        }
                    }
                }
            } // Repeat tab

        } // StackLayout
    }

    onAccepted: {
        root.action.status = status.currentIndex
        root.action.name = name.text;
        root.action.descr = descr.text
        root.action.priority = priority.currentIndex
        root.action.due = whenControl.due
        root.action.timeEstimate = Common.textToMinutes(timeEstimateCtl.text)
        root.action.difficulty = difficultyCtl.currentIndex
        root.action.repeatKind = repeatKindCtl.currentIndex
        root.action.repeatWhen = repeatWhenCtl.currentIndex
        root.action.favorite = favorite.isChecked
        root.action.category = category.uuid

        if (grid.showRepeatSpecCtl) {
            root.action.repeatAfter = createIntFromList(repeatSpecCtl.model)
            // console.log("RepeatAfter bits: ", root.action.repeatAfter)
            root.action.repeatUnits = 0
        } else if (grid.showRepeatAfterCtl) {
            root.action.repeatAfter = repeatAfterCtl.value
            root.action.repeatUnits = repeatUnitCtl.currentIndex
        } else {
            root.action.repeatAfter = 0
            root.action.repeatUnits = 0
        }

        if (root.action.id_proto !== "") { // edit
            ActionsModel.updateAction(root.action)
        } else {
            ActionsModel.addAction(root.action)
        }

        close()
    }

    onRejected: {
        close()
    }

    function createIntFromList(listModel) {
        let value = 0;
        for (let i = 0; i < listModel.count; i++) {
            var checked = listModel.get(i).checked
            // console.log("Checked", i, checked)
            if (checked) {
                value |= 1 << i;
            }
        }
        return value;
    }

    function updateListFromInt(listModel, value) {
        for (let i = 0; i < listModel.count; i++) {
            listModel.get(i).checked = ((value >> i) & 1) === 1;
        }
    }
}

