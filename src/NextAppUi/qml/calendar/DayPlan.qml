import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models


Rectangle {
    id: root
    property int hourHeight: 60
    property int leftMargin: 48
    implicitWidth: 400
    implicitHeight: hourHeight * 24
    //height: hourHeight * 24
    color: MaterialDesignStyling.surface
    property var model: null
    anchors.fill: parent

    Connections {
        target: model

        function onValidChanged() {
            console.log("DayPlan: Model Valid Changed ", model.valid)
            if (model.valid) {
                // Redraw the component
                //root.update();
                canvasCtl.requestPaint()
            }
        }
    }

    Rectangle {
        id: hourMarkers
        implicitHeight: parent.height
        implicitWidth: parent.leftMargin
        color: MaterialDesignStyling.primaryContainer
    }

    Canvas {
        id: canvasCtl
        anchors.fill: parent

        onPaint: {
            console.log("DayPlan: Redraing Canvas")

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

            ctx.beginPath();
            ctx.save()
            ctx.strokeStyle = MaterialDesignStyling.outlineVariant;
            ctx.setLineDash([2, 6]);
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
            ctx.restore()

            ctx.beginPath();
            ctx.save()
            ctx.setLineDash([2, 4]);
            ctx.lineWidth = 0.5
            ctx.strokeStyle = MaterialDesignStyling.outline;
            for (i = 1; i <= 24; i++) {
                y = i * hourHeight;
                {
                    ii = 2
                    yy = y - (ii * (hourHeight / 4));
                    ctx.moveTo(hourMarkers.width, yy);
                    ctx.lineTo(width, yy);
                }
            }
            ctx.stroke()
            ctx.restore()

            // Draw the events directly from C++
            model.addCalendarEvents()
        }
    }

    function timeToY(time) {
        var when = new Date(time * 1000)
        var minutes = when.getHours() * 60.0 + when.getMinutes();
        var y = minutes * (root.height / 1440.0)
        console.log("DayPlan: timeToY time=", time, "y=", y, ", height=", root.height, ", minutes=", minutes, ", when=", when)
        return Math.floor(y)
    }

    // We use a popup in stead of a rectangle to draw it on top of all anythign else in the calendar
    Popup {
        id: dragRectangle
        margins: 0
        modal: true
        opacity: 0.6

        background: Rectangle {
            color: height < 10 ? MaterialDesignStyling.errorContainer : MaterialDesignStyling.tertiaryContainer
            border.color: MaterialDesignStyling.outline
            border.width: height < 10 ? 0 : 1
            radius: 10
        }

        visible: false
        width: parent.width - (hourMarkers.width + 10) - 10
        height: 0
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: {
            dragRectangle.x = hourMarkers.width + 10
            dragRectangle.y = mouseY
            dragRectangle.height = 0
            dragRectangle.visible = true
        }
        onPositionChanged: {
            dragRectangle.height = mouseY - dragRectangle.y
            console.log("dragRectangle: x=",
                        dragRectangle.x, " y=",
                        dragRectangle.y, " w=",
                        dragRectangle.width, ", h=",
                        dragRectangle.height)
        }
        onReleased: {
            if (dragRectangle.height < 10) {
                dragRectangle.visible = false
                return
            }
            console.log("released")
            if (mouseY + timeboxPopup.height > root.height) {
                timeboxPopup.y = mouseY - timeboxPopup.height - 10
            } else {
                timeboxPopup.y = mouseY
            }
            timeboxPopup.open()
        }
    }

    Popup {
        id: timeboxPopup
        x: 10
        width: 400
        //height: 200
        modal: true
        //visible: false

        onClosed: {
            console.log("closed")
            dragRectangle.visible = false
        }

        onVisibleChanged: {
            if (visible) {
                title.focus = true
                title.text = ""
            }
        }

        background: Rectangle {
            color: MaterialDesignStyling.tertiaryContainer
        }
        contentItem: ColumnLayout {
            spacing: 10
            anchors.fill: parent

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 10
            }

            TextField {
                id: title
                Layout.fillWidth: true
                placeholderText: "Title"
                color: MaterialDesignStyling.onTertiaryContainer
            }

            StyledButton {
                width: 180
                text: qsTr("Category")
                onClicked: {
                    //filter.open()
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 10
            }

            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Button {
                    text: "Cancel"
                    onClicked: {
                        timeboxPopup.close()
                    }
                }
                Button {
                    text: "Save"
                    onClicked: {
                        root.model.createTimeBox(title.text, "",
                                                 toMinuteInDay(dragRectangle.y),
                                                 toMinuteInDay(dragRectangle.y + dragRectangle.height))
                        timeboxPopup.close()
                    }
                }
            }
        }
    }

    DropArea {
        anchors.fill: parent

        onEntered: (drag) => {
            console.log("TimeBlock/DropArea entered by ", drag.source.toString(), " types ", drag.formats)
            if (drag.formats.indexOf("text/app.nextapp.calendar.event") !== -1) {
                drag.accepted = true
            }
        }

        onDropped: (drop) => {
            if (drop.formats.indexOf("text/app.nextapp.calendar.event") !== -1) {
                let uuid = drop.getDataAsString("text/app.nextapp.calendar.event")
                let hour =  Math.floor(drop.y / root.hourHeight)
                let minute = Math.floor((drop.y % root.hourHeight) / (root.hourHeight / 60))
                console.log("Dropped calendar event ", uuid, " at x=", drop.x, ", y=", drop.y,
                            " hour=", hour, "minute=", minute)

                let new_time = new Date(root.model.year, root.model.month - 1, root.model.day)
                new_time.setHours(hour)
                new_time.setMinutes(getRoundedMinutes(minute))
                root.model.moveEventToDay(uuid, new_time.getTime() / 1000)
                drop.accepted = true
            }
        }
    }

    function getRoundedMinutes(minute) {
        const rounded = Math.round(minute / model.roundToMinutes) * model.roundToMinutes
        return rounded
    }

    function toMinuteInDay(y) {
        var one_minute = root.height / (24.0 * 60.0)

        return getRoundedMinutes(Math.floor(y / one_minute))
    }
}
