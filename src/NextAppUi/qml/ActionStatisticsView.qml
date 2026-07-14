import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NextAppUi
import Nextapp.Models

ColumnLayout {
    id: root
    property ActionStatsModelPtr model: null

    Connections {
        target: model
        function onValidChanged() {
            if (model && model.valid)
                checkBox.checked = model.model.withOrigin;
        }
    }


    // headline
    Text {
        text: model !== null ? model.model.actionInfo.name : qsTr("No data")
        font.pointSize: 16
        font.bold: true
    }

    CheckBox {
        id: checkBox
        text: qsTr("Recursively track this action")
        checked: false
        enabled: model?.valid

        onCheckedChanged: {
            root.model.model.withOrigin = checked;
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        uniformCellWidths: false

        Label {
            text: qsTr("Started date")
        }

        Text {
            text: model !== null ? model.model.firstSessionDate : qsTr("Not started")
        }

        Label {
            text: qsTr("Last date")
        }

        Text {
            text: model?.valid ? model.model.lastSessionDate : ""
        }

        Label {
            text: qsTr("Time spent")
        }

        Text {
            text: model?.valid ? NaCore.toHourMin(model.model.totalMinutes * 60) : ""
        }

        Label {
            text: qsTr("Number of days tracked")
        }

        Text {
            text: model?.valid ? model.model.daysCount : ""
        }
    }

    TimelineGraph {
        id: graph
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: model?.valid === true && hasData
        points: model?.valid ? model.model.workInDays : []
        seriesColor: MaterialDesignStyling.primary
        axisColor: MaterialDesignStyling.outline
        gridColor: MaterialDesignStyling.outlineVariant
        textColor: MaterialDesignStyling.onSurfaceVariant
    }

    Item {
        Layout.fillHeight: true
        visible: !graph.visible
    }
}
