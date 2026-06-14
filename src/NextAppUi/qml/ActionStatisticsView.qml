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
    property bool graphReady: false

    Connections {
        target: model
        function onValidChanged() {
            if (!model || !model.valid) {
                root.graphReady = false;
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
        visible: model?.valid === true && root.graphReady

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
        root.graphReady = false;

        if (!model || !model.model)
            return;

        const days = model.model.workInDays;
        if (!days || days.length < 2)
            return;

        console.log("days=", days, " num days=", days.length);

        let maxY = 0;
        let minX = Number.POSITIVE_INFINITY;
        let maxX = Number.NEGATIVE_INFINITY;
        let points = 0;

        for (let i = 0; i < days.length; ++i) {
            const entry = days[i];
            if (!entry || !entry.date)
                continue;

            const date = entry.date instanceof Date ? entry.date : new Date(entry.date);
            const x = date.getTime();
            if (!Number.isFinite(x))
                continue;

            const y = Number(entry.minutes || 0) / 60.0;

            data.append(x, y);
            ++points;
            minX = Math.min(minX, x);
            maxX = Math.max(maxX, x);

            if (y > maxY)
                maxY = y;
        }

        if (points < 2 || !Number.isFinite(minX) || !Number.isFinite(maxX) || minX >= maxX) {
            data.clear();
            return;
        }

        // Adjust axis if present
        if (graph.axisY) {
            graph.axisY.min = 0;
            graph.axisY.max = Math.max(maxY * 1.1, 8); // give some headroom
        }

        // Set the X axis to the first and last valid dates.
        if (graph.axisX) {
            graph.axisX.min = new Date(minX);
            graph.axisX.max = new Date(maxX);
        }

        root.graphReady = true;
    }
}
