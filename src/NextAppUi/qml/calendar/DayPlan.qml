import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models

Rectangle {
    id: root
    property int hourHeight: 60
    Layout.fillHeight: true
    color: MaterialDesignStyling.surface
    property var model: null

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

        // function onTimeChanged() {
        //     console.log("DayPlan: Model Time Changed ", model.when)
        //     //canvasCtl.requestPaint()
        //     //currentTimeMark.requestPaint()
        // }

        function onTodayChanged() {
            console.log("DayPlan: Model Today Changed ", model.today)
        }
    }

    Canvas {
        id: canvasCtl
        anchors.fill: parent

        onPaint: {
            //console.log("DayPlan: Redraing Canvas")

            var ctx = getContext("2d");
            ctx.beginPath();
            ctx.strokeStyle = MaterialDesignStyling.outline;
            ctx.lineWidth = 1;
            for (var i = 1; i <= 24; i++) {
                var y = i * hourHeight;
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
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
                    ctx.moveTo(0, yy);
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
                    ctx.moveTo(0, yy);
                    ctx.lineTo(width, yy);
                }
            }
            ctx.stroke()
            ctx.restore()

            // Draw the events directly from C++
            model.addCalendarEvents()
        }
    }

    Rectangle {
        id: currentTimeMarkBg
        x: 0
        width: parent.width
        height: 7
        color: "green"
        opacity: 0.3
        visible: model.today
        y: minuteToY(root.model.now) - 3
        z: 990
    }

    Rectangle {
        anchors.fill: currentTime
        visible: model.today
        color: "green"
        opacity: 0.8
        radius: 5
        z: 990
    }

    Text {
        id: currentTime
        text: root.model.timeStr
        color: "white"
        font.pixelSize: 12
        visible: model.today
        y: currentTimeMarkBg.y - 15
        x: parent.width - implicitWidth - 10
        z: 995
    }

    Rectangle {
        id: currentTimeMark
        x: 0
        width: parent.width
        height: 1
        color: "green"
        visible: model.today
        y: currentTimeMarkBg.y + 3
        z: 1000
    }

    function timeToY(time) {
        var when = new Date(time * 1000)
        var minutes = when.getHours() * 60.0 + when.getMinutes();
        return minuteToY(minutes)
    }

    function minuteToY(minutes) {
        var y = minutes * (root.height / (root.hourHeight * 24.0))
        //console.log("DayPlan: minuteToY minutes=", minutes, "y=", y, ", height=", root.height, ", minutes=", minutes, ", when=", when)
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
        width: parent.width
        height: 0
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: {
            dragRectangle.x = 0
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

            // StyledButton {
            //     width: 180
            //     text: qsTr("Category")
            //     onClicked: {
            //         //filter.open()
            //     }
            // }

            CategoryComboBox {
                id: category
                Layout.fillWidth: true
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
                        root.model.createTimeBox(title.text, category.uuid,
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

                let new_time = new Date(root.model.when * 1000)
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
