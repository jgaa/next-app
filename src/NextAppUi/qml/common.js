.pragma library
.import QtQuick as QtQuick

function createDialog(name, parent, args) {
    var ctl = Qt.createComponent(name)
    if (ctl.status !== QtQuick.Component.Ready) {
        console.log("Error loading component ", name, ": ", ctl.errorString())
        return null
    }
    var dialog = ctl.createObject(parent, args)
    if (dialog === null) {
        console.log("Error creating dialog ", name)
        return null
    }

    return dialog
}

function openDialog(name, parent, args, bind) {
    var dialog = createDialog(name, parent, args)
    if (dialog) {
        dialog.open()
    }
    return dialog
}
