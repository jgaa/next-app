import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPb
import "common.js" as Common
import Nextapp.Models

ColumnLayout {
    id: root
    property int controlsPreferredWidth: (width - 40 - leftMarginForControls) / (NaCore.isMobile ? 1 : 4)
    property int labelWidth: 80
    property int leftMarginForControls: NaCore.isMobile ? 20 : 0
    property NextappPb.action action: null
    property bool existingOnly: false
    property bool autoCommit: false

    function assign(newAction) {
        commitIf()
        root.action = newAction
        if (root.action === null) {
            // Empty all the contrrols
            name.text = ""
            descr.text = ""
            status.currentIndex = 0
            priority.currentIndex = 0
            createdDateCtl.text = ""
            timeEstimateCtl.text = ""
            whenCtl.currentIndex =  NextappPb.Due.Kind.UNSET
            difficultyCtl.currentIndex = 0
            repeatWhenCtl.currentIndex = 0
            repeatUnitCtl.currentIndex = 0
            repeatKindCtl.currentIndex = 0
            favorite.isChecked = false
            category.uuid = ""
            repeatAfterCtl.value = 1
            priority.mode = 0
            priority.priority = 5
            completedTimeCtl.text = ""
            whenControl.due = NaActionsModel.createDue(0, 0)
            whenCtl.setWhenCurrentIndex(whenControl.due.kind)
            shortcuts.currentIndex = -1
            repeatSpecCtl.model.forEach(function(item) {
                item.checked = false
            })
            return
        }

        console.log("EditActionDlg/assign action=", root.action)

        status.currentIndex = root.action.status
        name.text = root.action.name = action.name
        descr.text = root.action.descr

        if (root.action.dynamicPriority.hasPriority) {
            priority.mode = 0
            priority.priority = root.action.dynamicPriority.priority
        } else if (root.action.dynamicPriority.hasUrgencyImportance) {
            priority.mode = 1
            priority.urgency = root.action.dynamicPriority.urgencyImportance.urgency
            priority.importance = root.action.dynamicPriority.urgencyImportance.importance
        }

        createdDateCtl.text = Common.formatPbDate(root.action.createdDate)
        timeEstimateCtl.text = Common.minutesToText(root.action.timeEstimate)
        difficultyCtl.currentIndex = root.action.difficulty
        repeatWhenCtl.currentIndex = root.action.repeatWhen
        repeatUnitCtl.currentIndex = root.action.repeatUnits
        repeatKindCtl.currentIndex = root.action.repeatKind
        //whenCtl.currentIndex = root.action.due.kind
        whenCtl.due = root.action.due
        whenCtl.setWhenCurrentIndex(whenCtl.due.kind)
        //whenCtl.displayText = NaActionsModel.formatDue(root.action.due)
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
            if (action.id_proto === "" && action.category === "") {
                action.category = NaMainTreeModel.getCategoryForNode(node, true)
                category.uuid = action.category
            }
        }

        if (action.node === "") {
            throw "No node"
        }
    }

    function update() {
        root.action.status = status.currentIndex
        root.action.name = name.text;
        root.action.descr = descr.text
        //root.action.priority = priority.currentIndex

        if (priority.mode === 0) {
            console.log("EditActionView: setting priority ", priority.priority)
            root.action.dynamicPriority.priority = priority.priority
        } else if (priority.mode === 1) {
             console.log("EditActionView: setting urgency ", priority.urgency,
                        " importance ", priority.importance)

            root.action.dynamicPriority.urgencyImportance = NaActionsModel.setUrgencyImportance(
                priority.urgency,
                priority.importance)

            // Don't work!
            // root.action.dynamicPriority.urgencyImportance.urgency = priority.urgency
            // root.action.dynamicPriority.urgencyImportance.importance = priority.importance
        }

        root.action.due = whenCtl.due
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
    }

    function commit() {
        update()
        doCommit()
    }

    function doCommit() {
        if (root.action.id_proto !== "") { // edit
            NaActionsModel.updateAction(root.action)
        } else if (!existingOnly) {
            NaActionsModel.addAction(root.action)
        }
    }

    function hasChanged() {
        const before = JSON.stringify(root.action);
        update()
        return before !== JSON.stringify(root.action)
    }

    function commitIf() {
        if (root.action.id_proto !== "" && autoCommit && hasChanged()) {
            doCommit()
        }
    }

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
                //Layout.fillHeight: true
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

            ScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ScrollBar.horizontal.policy: ScrollBar.AlwaysOn
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn

                // ScrollBar.vertical: ScrollBar {
                //     policy: ScrollBar.AlwaysOn
                // }

                TextArea {
                    id: descr
                    topInset: 6
                    Layout.leftMargin: root.leftMarginForControls
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    clip: true
                    placeholderText: qsTr("Description")
                    wrapMode: TextEdit.Wrap

                    //text: root.action.descr
                    // background: Rectangle {
                    //     color: descr.focus ? "lightblue" : "lightgray"
                    // }
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

                WhenSelector {
                    id: whenCtl
                    due: root.action.due
                    maybeKind: due.kind
                    Layout.fillWidth: true
                    Layout.leftMargin: root.leftMarginForControls
                }

                MoveDue {
                    id: moveDue
                    due: root.action.due || NaActionsModel.getEmptyDue()
                    Layout.fillWidth: true
                    Layout.leftMargin: root.leftMarginForControls

                    onDueValueChanged: (due) => {
                        whenCtl.due = due
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
                    Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                    color: Colors.disabledText
                    text: qsTr("Priority")
                }

                PrioritySelector {
                    id: priority
                    Layout.leftMargin: root.leftMarginForControls
                    Layout.preferredWidth: root.controlsPreferredWidth
                }

                Label {
                    Layout.alignment: Qt.AlignLeft
                    color: Colors.disabledText
                    text: qsTr("Score")
                }

                Text {
                    Layout.leftMargin: root.leftMarginForControls
                    text: root.action.score
                }

                Label {
                    Layout.alignment: Qt.AlignLeft
                    color: Colors.disabledText
                    text: qsTr("Difficulty")
                }

                DifficultySelector {
                    id: difficultyCtl
                    Layout.leftMargin: root.leftMarginForControls
                    Layout.preferredWidth: root.controlsPreferredWidth
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
