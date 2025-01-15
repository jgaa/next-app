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
    closePolicy: Popup.NoAutoClose
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
        }
    }

    function assign() {
        if (aprx.valid && !root.assigned) {

            actionsCtl.assign(aprx.action)
            // Don't do it again for this instance
            root.assigned = true
        }
    }

    EditActionView {
        id: actionsCtl
        anchors.fill: parent
    }

    onAccepted: { 
        actionsCtl.commit()
        close()
    }

    onRejected: {
        close()
    }
}

