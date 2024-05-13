import QtQuick
import QtQuick.Controls
import NextAppUi

Rectangle {
    property int hourHeight: 60
    implicitWidth: 400
    implicitHeight: hourHeight * 24
    //height: hourHeight * 24
    color: MaterialDesignStyling.surface

    Rectangle {
        id: hourMarkers
        implicitHeight: parent.height
        implicitWidth: 48
        color: MaterialDesignStyling.primaryContainer
    }

    Canvas {
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d");
            ctx.strokeStyle = MaterialDesignStyling.outline;
            ctx.beginPath();
            //ctx.font = "16px Arial"; // Don't work!
            ctx.lineWidth = 1;
            ctx.setLineDash([]);
            for (var i = 1; i <= 24; i++) {
                var y = i * hourHeight;
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.fillText(i, 5, y - hourHeight + 19);
            }
            ctx.stroke()
            ctx.beginPath();

            ctx.strokeStyle = MaterialDesignStyling.onPrimaryContainer
            ctx.color = "yellow"
            for (i = 1; i <= 24; i++) {
                y = i * hourHeight;
                ctx.fillText(i, 5, y - hourHeight + 19);
            }
            ctx.stroke()
            ctx.beginPath();

            ctx.strokeStyle = MaterialDesignStyling.outlineVariant;
            ctx.setLineDash([3, 3]);
            ctx.lineWidth = 0.5
            for (i = 1; i <= 24; i++) {
                y = i * hourHeight;
                for(var ii = 1; ii < 4; ++ii) {
                    if (ii == 2)
                        continue;
                    var yy = y - (ii * (hourHeight / 4));
                    ctx.moveTo(hourMarkers.width, yy);
                    ctx.lineTo(width, yy);
                }
            }
            ctx.stroke()
            ctx.beginPath();

            ctx.setLineDash([3, 3]);
            ctx.lineWidth = 1
            ctx.strokeStyle = MaterialDesignStyling.outline;
            for (i = 1; i <= 24; i++) {
                y = i * hourHeight;
                {
                    ii = 2
                    yy = y - (ii * (hourHeight / 4));
                    ctx.lineWidth = 0.5
                    ctx.moveTo(hourMarkers.width, yy);
                    ctx.lineTo(width, yy);
                }
            }
            ctx.stroke()
        }
    }
}
