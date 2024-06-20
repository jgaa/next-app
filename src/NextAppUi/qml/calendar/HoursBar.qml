import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi

Rectangle {
    id: root
    property int hourHeight: 60

    Layout.fillHeight: true
    Layout.preferredWidth: 48

    color: MaterialDesignStyling.primaryContainer

    Canvas {
        id: canvasCtl
        anchors.fill: parent

        onPaint: {
            // console.log("HoursBar: Redraing Canvas")

            var ctx = getContext("2d");
            ctx.beginPath();
            ctx.strokeStyle = MaterialDesignStyling.outline;
            ctx.lineWidth = 1;
            ctx.fillStyle = MaterialDesignStyling.onPrimaryContainer
            ctx.font = "16px sans-serif";
            for (var i = 1; i <= 24; i++) {
                var y = i * hourHeight;
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.fillText(i - 1, 5, y - hourHeight + 19);
            }
            ctx.stroke()

            // ctx.beginPath();
            // ctx.save()
            // ctx.strokeStyle = MaterialDesignStyling.outlineVariant;
            // ctx.setLineDash([2, 6]);
            // ctx.lineWidth = 0.5
            // for (i = 1; i <= 24; i++) {
            //     y = i * hourHeight;
            //     for(var ii = 1; ii < 4; ++ii) {
            //         if (ii == 2)
            //             continue;
            //         var yy = y - (ii * (hourHeight / 4));
            //         ctx.moveTo(hourMarkers.width, yy);
            //         ctx.lineTo(width, yy);
            //     }
            // }
            // ctx.stroke()
            // ctx.restore()

            // ctx.beginPath();
            // ctx.save()
            // ctx.setLineDash([2, 4]);
            // ctx.lineWidth = 0.5
            // ctx.strokeStyle = MaterialDesignStyling.outline;
            // for (i = 1; i <= 24; i++) {
            //     y = i * hourHeight;
            //     {
            //         ii = 2
            //         yy = y - (ii * (hourHeight / 4));
            //         ctx.moveTo(hourMarkers.width, yy);
            //         ctx.lineTo(width, yy);
            //     }
            // }
            // ctx.stroke()
            // ctx.restore()

            // // Draw the events directly from C++
            // model.addCalendarEvents()
        }
    }
}
