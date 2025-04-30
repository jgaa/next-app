import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NextAppUi
import nextapp.pb as NextappPb
import "common.js" as Common
import Nextapp.Models

Dialog {
    x: NaCore.isMobile ? 0 : (parent.width - width) / 3
    y: NaCore.isMobile ? 0 : (parent.height - height) / 3
    width: NaCore.isMobile ? parent.width : Math.min(parent.width, 800)
    height: NaCore.isMobile ? parent.height : Math.min(parent.height - 10, 600)
    property alias model : actionStatistics.model
    standardButtons: DialogButtonBox.Close
    title: qsTr("Action Work-Statistics")

    ActionStatisticsView {
        id: actionStatistics
        anchors.fill: parent
    }
}
