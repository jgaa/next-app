import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi
import Nextapp.Models

Rectangle {
    id: root
    color: MaterialDesignStyling.surface
    Layout.fillWidth: true
    Layout.fillHeight: true

    ActionsListView {
        anchors.fill: parent
    }
}
