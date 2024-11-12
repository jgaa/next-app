import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts
import NextAppUi
import Nextapp.Models

ColumnLayout {
    id: root
    property alias model: listView.model

    ListView {
        id: listView
        Layout.fillHeight: true
        Layout.fillWidth: true
        interactive: false
        clip: true

        delegate: Rectangle {
            required property int index
            required property string name
            required property string colorName
            required property int minutes
            implicitHeight: 20
            implicitWidth: listView.width
            color: index % 2 ? MaterialDesignStyling.secondaryContainer : MaterialDesignStyling.primaryContainer

            RowLayout {
                id: rowLayout
                Rectangle {
                    Layout.preferredHeight: nameCtl.implicitHeight
                    Layout.preferredWidth: 10
                    color: colorName
                }

                Text {
                    id: nameCtl
                    text: name
                    color: MaterialDesignStyling.onPrimaryContainer
                }

                Text {
                    text: NaCore.toHourMin(minutes * 60)
                    color: MaterialDesignStyling.onPrimaryContainer
                }
            }
        }
    }
}
