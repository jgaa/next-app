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
    property int controlsPreferredWidth: (width - 40 - leftMarginForControls) / (NaCore.isMobile ? 1 : 4)
    property int labelWidth: 80
    property int leftMarginForControls: NaCore.isMobile ? 20 : 0

    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(700, NaCore.height, Screen.height)

    standardButtons: root.aprx.valid ? (Dialog.Ok | Dialog.Cancel) : Dialog.Cancel

    onOpened: {
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

    MouseArea {
        anchors.fill: parent
        onClicked: {
            // Handle the click event or simply ignore it
            console.log("MouseArea clicked")
        }
    }

    // Disable drag events
    DragHandler {
        acceptedButtons: []
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
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                GridLayout {
                    id: dlgfields
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    uniformCellWidths: false
                    rowSpacing: 4
                    columns: NaCore.isMobile ? 1 : 2

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("Name")
                        }
                    }

                    DlgInputField {
                        Layout.leftMargin: root.leftMarginForControls
                        id: name
                        Layout.fillWidth: true
                    }
                }

                //RowLayout {
                GridLayout {
                    Layout.fillWidth: true
                    columns: NaCore.isMobile ? 1 : 4
                    uniformCellWidths: false

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("Status")
                        }
                    }

                    ComboBox {
                        id: status
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.preferredWidth: root.controlsPreferredWidth
                        model: ListModel {
                            ListElement{ text: qsTr("Active")}
                            ListElement{ text: qsTr("Done")}
                            ListElement{ text: qsTr("On Hold")}
                        }
                    }

                    CategoryComboBox {
                        id: category
                        Layout.leftMargin: root.leftMarginForControls
                        //Layout.preferredWidth: root.controlsPreferredWidth
                        Layout.fillWidth: true
                    }

                    CheckBoxWithFontIcon {
                        id: favorite
                        Layout.leftMargin: root.leftMarginForControls
                        //Layout.preferredWidth: root.controlsPreferredWidth
                        checkedCode: "\uf005"
                        uncheckedCode: "\uf005"
                        checkedColor: "orange"
                        uncheckedColor: "lightgray"
                        useSolidForChecked: true
                        text: qsTr("Favorite")
                    }

                }


                GridLayout {
                    Layout.fillWidth: true
                    columns: NaCore.isMobile ? 1 : 4

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("When")
                        }
                    }

                    WhenControl {
                        id: whenControl
                        due: root.action.due
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.preferredWidth: root.controlsPreferredWidth

                        onSelectionChanged: {
                            // console.log("DueType changed to", whenControl.due.kind)
                            //root.action.due = whenControl.due
                            shortcuts.currentIndex = -1
                        }
                    }

                    ComboBox {
                        id: shortcuts
                        //Layout.preferredWidth: root.controlsPreferredWidth * (NaCore.isMobile ? 1 : 2)
                        Layout.fillWidth: true
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.rowSpan: NaCore.isMobile ? 1 : 2

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

                GridLayout {
                    Layout.fillWidth: true
                    columns: NaCore.isMobile ? 1 : 2

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("Description")
                        }
                    }

                    TextArea {
                        id: descr
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        //placeholderText: qsTr("Some words to describe the purpose of this item?")
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
                    columns: NaCore.isMobile ? 1 : 2

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("Created")
                        }
                    }

                    Text {
                        Layout.leftMargin: root.leftMarginForControls
                        id: createdDateCtl
                        color: Colors.disabledText
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Completed")
                    }

                    Text {
                        Layout.leftMargin: root.leftMarginForControls
                        id: completedTimeCtl
                        color: Colors.disabledText
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("uuid")
                    }

                    TextInput  {
                        Layout.leftMargin: root.leftMarginForControls
                        text: root.action.id_proto
                        color: Colors.disabledText
                        readOnly: true
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        color: Colors.disabledText
                        text: qsTr("Time Estimate")
                    }

                    RowLayout {
                        Layout.leftMargin: root.leftMarginForControls
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
                        text: qsTr("Priority")
                    }

                    ComboBox {
                        id: priority
                        Layout.leftMargin: root.leftMarginForControls
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
                        text: qsTr("Difficulty")
                    }

                    ComboBox {
                        id: difficultyCtl
                        Layout.leftMargin: root.leftMarginForControls
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
                } // Grid Layout
            } // Details tab

            // Repeat tab
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                GridLayout {
                    id: grid
                    property int colWidth: NaCore.isMobile ? (root.controlsPreferredWidth)
                                                           : (root.controlsPreferredWidth * 2)
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    rowSpacing: 4
                    columns: NaCore.isMobile ? 1 : 2

                    property bool showControls: repeatKindCtl.currentIndex > 0

                    property bool showRepeatAfterCtl :
                        showControls && repeatWhenCtl.currentIndex === 0

                    property bool showRepeatSpecCtl :
                        showControls && repeatWhenCtl.currentIndex === 1

                    ColumnLayout {
                        Layout.column: 0 // This is the first column
                        Label {
                            Layout.preferredWidth: root.labelWidth
                            color: Colors.disabledText
                            text: qsTr("Repeat")
                        }
                    }

                    ComboBox {
                        id: repeatKindCtl
                        Layout.preferredWidth: grid.colWidth
                        Layout.leftMargin: root.leftMarginForControls
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
                        Layout.leftMargin: root.leftMarginForControls
                        visible: grid.showControls
                        Layout.preferredWidth: grid.colWidth
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
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.preferredWidth: grid.colWidth
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
                            //Layout.fillWidth: true
                            model: ListModel {
                                ListElement{ text: qsTr("Days")}
                                ListElement{ text: qsTr("Weeks")}
                                ListElement{ text: qsTr("Months")}
                                ListElement{ text: qsTr("Quarters")}
                                ListElement{ text: qsTr("Years")}
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    Item {
                        visible: grid.showRepeatSpecCtl && !NaCore.isMobile
                    }

                    Item {
                        visible: !grid.showRepeatSpecCtl
                        //Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    ListView {
                        id: repeatSpecCtl
                        Layout.leftMargin: root.leftMarginForControls
                        Layout.preferredWidth: grid.colWidth
                        //width: grid.colWidth
                        Layout.fillHeight: true
                        clip: true
                        visible: grid.showRepeatSpecCtl

                        onVisibleChanged: {
                            console.log("RepeatSpecCtl visible:", visible)
                            width: visible ? grid.colWidth : 0
                            Layout.fillWidth = true
                            console.log("x=", x, "y=", y, "width=", width, "height=", height)
                        }

                        ScrollBar.vertical: ScrollBar {
                            id: vScrollBar
                            parent: repeatSpecCtl
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: MaterialDesignStyling.scrollBarWidth
                            policy: ScrollBar.AlwaysOn
                        }

                        // Rectangle {
                        //     id: background
                        //     color: "gray"
                        //     anchors.fill: parent
                        // }

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
                            width: repeatSpecCtl.width - vScrollBar.width
                            height: checkBox.height
                            visible: true
                            CheckBox {
                                id: checkBox
                                visible: true
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

