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
    modal: true
    property string node: NaMainTreeModel.selected
    property ActionPrx aprx
    //property NextappPb.action action: aprx.action
    property alias action: actionsCtl.action
    property bool assigned: false
    property bool valid: aprx.valid
    property bool dragWasEnabled: false

    x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    width: Math.min(600, NaCore.width, Screen.width)
    height: Math.min(700, NaCore.height, Screen.height)

    standardButtons: root.aprx.valid ? (Dialog.Ok | Dialog.Cancel) : Dialog.Cancel

    onOpened: {
        assign()
        if (NaCore.dragEnabled) {
            NaCore.dragEnabled = false
            dragWasEnabled = true
        }
    }

    onClosed: {
        if (dragWasEnabled) {
            NaCore.dragEnabled = true
        }
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

            actionsCtl.assign(aprx.action)

            // //root.action = aprx.action

            // // Set the values in the controld. We can't bind them directly for some reason.
            // status.currentIndex = root.action.status
            // name.text = root.action.name = action.name
            // descr.text = root.action.descr
            // priority.currentIndex = root.action.priority
            // createdDateCtl.text = Common.formatPbDate(root.action.createdDate)
            // timeEstimateCtl.text = Common.minutesToText(root.action.timeEstimate)
            // difficultyCtl.currentIndex = root.action.difficulty
            // repeatWhenCtl.currentIndex = root.action.repeatWhen
            // repeatUnitCtl.currentIndex = root.action.repeatUnits
            // repeatKindCtl.currentIndex = root.action.repeatKind
            // favorite.isChecked = root.action.favorite
            // category.uuid = root.action.category

            // // console.log("EditActionDlg/assign category=", root.action.category)

            // if (root.action.repeatWhen === 0 /* AT_DATE */) {
            //     repeatAfterCtl.value = root.action.repeatAfter
            // } else {
            //     updateListFromInt(repeatSpecCtl.model, root.action.repeatAfter)
            // }

            // if (root.action.completedTime !== 0) {
            //     completedTimeCtl.text = Common.formatPbTimeFromTimet(root.action.completedTime)
            // } else {
            //     completedTimeCtl.text = qsTr("Not completed yet")
            // }

            // if (action.node === "") {
            //     action.node = node;
            // }

            // if (action.node === "") {
            //     throw "No node"
            // }

            // Don't do it again for this instance
            root.assigned = true
        }
    }

    EditActionView {
        id: actionsCtl
        anchors.fill: parent
    }

    onAccepted: {
        // root.action.status = status.currentIndex
        // root.action.name = name.text;
        // root.action.descr = descr.text
        // root.action.priority = priority.currentIndex
        // root.action.due = whenControl.due
        // root.action.timeEstimate = Common.textToMinutes(timeEstimateCtl.text)
        // root.action.difficulty = difficultyCtl.currentIndex
        // root.action.repeatKind = repeatKindCtl.currentIndex
        // root.action.repeatWhen = repeatWhenCtl.currentIndex
        // root.action.favorite = favorite.isChecked
        // root.action.category = category.uuid

        // if (grid.showRepeatSpecCtl) {
        //     root.action.repeatAfter = createIntFromList(repeatSpecCtl.model)
        //     // console.log("RepeatAfter bits: ", root.action.repeatAfter)
        //     root.action.repeatUnits = 0
        // } else if (grid.showRepeatAfterCtl) {
        //     root.action.repeatAfter = repeatAfterCtl.value
        //     root.action.repeatUnits = repeatUnitCtl.currentIndex
        // } else {
        //     root.action.repeatAfter = 0
        //     root.action.repeatUnits = 0
        // }

        // if (root.action.id_proto !== "") { // edit
        //     NaActionsModel.updateAction(root.action)
        // } else {
        //     NaActionsModel.addAction(root.action)
        // }

        actionsCtl.commit()

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

