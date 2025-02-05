import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    title: qsTr("Application log")
    standardButtons: Dialog.Close
    x: NaCore.isMobile ? 0 : (ApplicationWindow.window.width - width) / 3
    y: NaCore.isMobile ? 0 : (ApplicationWindow.window.height - height) / 3
    width: Math.min(ApplicationWindow.window.width, 900)
    height: Math.min(ApplicationWindow.window.height - 10, 1200)

    LogView {
        anchors.fill: parent
    }
}
