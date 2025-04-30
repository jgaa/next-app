import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtGraphs
import NextAppUi
import nextapp.pb as NextappPb
import "common.js" as Common
import Nextapp.Models

ColumnLayout {
    id: root
    property ActionStatsModelPtr model: null

    Connections {
        target: model
        function onValidChanged() {
            if (!model || !model.valid) {
                return
            }
            updateGraph();
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

    GraphsView {
        id: graph
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: model?.valid === true && model.model.daysCount > 1

        ScatterSeries {
            id: data
            color: "#00ff00"
        }

        axisX: DateTimeAxis {
            labelFormat: "dd MMM yy"
            min: "2025-01-01"
            max: "2025-04-01"
        }

        axisY: ValueAxis {
            min: 0
            max: 8 // will be updated dynamically
        }
    }

    Item {
        Layout.fillHeight: true
        visible: !graph.visible
    }


    function updateGraph() {
        console.log("updateGraph()");
        data.clear();

        if (!model || !model.model || model.model.workInDays < 2)
            return;

        const days = model.model.workInDays;
        console.log("days=", days, " num days=", days.length);

        let maxY = 0;

        for (let i = 0; i < days.length; ++i) {
            const entry = days[i];
            if (!entry || !entry.date || !entry.minutes)
                continue;

            // X = date (timestamp in ms), Y = minutes
            const x = entry.date;
            const y = entry.minutes / 60.0;

            data.append(x, y);

            if (y > maxY)
                maxY = y;
        }

        // Adjust axis if present
        if (graph.axisY) {
            graph.axisY.min = 0;
            graph.axisY.max = Math.max(maxY * 1.1, 8); // give some headroom
        }

        // Set the X axix to the date from start to end
        if (graph.axisX && graph.axisX instanceof DateTimeAxis) {
            const startDate = days[0].date;
            const endDate = days[days.length - 1].date;
            graph.axisX.min = startDate;
            graph.axisX.max = endDate;
        }
    }
}
