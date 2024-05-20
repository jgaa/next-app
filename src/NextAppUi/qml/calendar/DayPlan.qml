import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import nextapp.pb as NextappPB
import Nextapp.Models


Rectangle {
    id: root
    property int hourHeight: 60
    implicitWidth: 400
    implicitHeight: hourHeight * 24
    //height: hourHeight * 24
    color: MaterialDesignStyling.surface
    property var model: null

    Connections {
        target: model

        onValidChanged: {
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
        implicitWidth: 48
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

            //addCalendarEvents()
        }
    }

    // function addCalendarEvents() {
    //     if (model == null)
    //         return

    //     for (var i = 0; i < root.model.size(); i++) {
    //         var event = root.model.event(i)
    //         var name = ""
    //         if (event.hasTimeBlock) {
    //             name = event.timeBlock.name
    //         }
    //         console.log("Event #", i, ", id=", event.id_proto, " is ", name)
    //     }
    // }

    Repeater {
        model: root.model

        TimeBox {
            id: timeBox
            timeBlock: root.model.valid ? root.model.event(root.index).timeBlock : null
            //height: (timeBox.timeBlock.end - timeBox.timeBlock.start) * root.hourHeight / 60
            height: 100
            y: timeBlock !== null ? timeToY(timeBlock.timeSpan.start) : 0
            //y: 500
            x: hourMarkers.width - 5
            width: root.width - x - 5

            onTimeBlockChanged: {
                console.log("DayPlan: TimeBlockChanged ", timeBlock, " start=", timeBlock.timeSpan.start, " end=", timeBlock.timeSpan.end)
            }
        }
    }

    function timeToY(time) {
        var when = new Date(time * 1000)
        var minutes = when.getHours() * 60.0 + when.getMinutes();
        var y = minutes * (root.height / 1440.0)
        console.log("DayPlan: timeToY time=", time, "y=", y, ", height=", root.height, ", minutes=", minutes, ", when=", when)
        return Math.floor(y)
    }

    Rectangle {
        id: dragRectangle
        color: height < 10 ? MaterialDesignStyling.errorContainer : MaterialDesignStyling.tertiaryContainer
        border.color: MaterialDesignStyling.outline
        border.width: height < 10 ? 0 : 1
        opacity: 0.6
        radius: 10
        visible: false
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: {
            dragRectangle.x = hourMarkers.width + 10
            dragRectangle.y = mouseY
            dragRectangle.width = parent.width - dragRectangle.x - 10
            dragRectangle.height = 0
            dragRectangle.visible = true
        }
        onPositionChanged: {
            //dragRectangle.width = mouseX - dragRectangle.x
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
            color: MaterialDesignStyling.teritaryContainer
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
                color: MaterialDesignStyling.onTeritaryContainer
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

    function toMinuteInDay(y) {
        var one_minute = root.height / (24.0 * 60.0)

        return Math.floor(y / one_minute)
    }
}
