import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
            color: index % 2
                   ? MaterialDesignStyling.surface
                   : MaterialDesignStyling.surfaceContainerHigh

            RowLayout {
                Rectangle {
                    Layout.preferredHeight: nameCtl.implicitHeight
                    Layout.preferredWidth: 10
                    color: colorName
                }
                Text {
                    id: nameCtl
                    text: name
                    color: MaterialDesignStyling.onSurface
                }
                Text {
                    text: NaCore.toHourMin(minutes * 60)
                    color: MaterialDesignStyling.onSurface
                }
            }
        }
    }


    DonutChart {
        id: pieChart
        Layout.fillWidth: true
        Layout.preferredHeight: root.width * 0.5
        model: root.model
        backgroundColor: MaterialDesignStyling.surface
        holeRatio: 0.6
    }
}
