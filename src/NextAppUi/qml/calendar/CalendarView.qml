import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models
import "../common.js" as Common

Rectangle {
    id: root
    Layout.fillHeight: true
    Layout.fillWidth: true
    property int hourHeight: 60
    color: MaterialDesignStyling.surface
    enabled: NaComm.connected
    opacity: NaComm.connected ? 1.0 : 0.5
    property CalendarModel model: NaCore.createCalendarModel()
    property bool inilializedModel: false
    property var when: new Date()
    property int mode: CalendarModel.CM_DAY
    property int days: 1
    property bool primaryForActionList: false

    onVisibleChanged: {
        // console.log("CalendarView: Visible changed to ", visible)
        initModel()
    }

    Component.onCompleted: {
        // console.log("CalendarView: Completed")
        initModel()
    }

    // Lazy initialization
    function initModel() {
        if (root.visible && !root.inilializedModel) {
            root.inilializedModel = true
            // console.log("Visible. model is valid=", root.model.valid)
            refresh()
        }
    }

    function refresh() {
        root.model.set(root.mode, root.when.getFullYear(), root.when.getMonth() +1, root.when.getDate(), primaryForActionList)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: navigation
            height: 28
            Layout.fillWidth: true
            color: MaterialDesignStyling.primary

            RowLayout {
                anchors.fill: parent
                Button {
                    height: navigation.height - 4
                    Layout.preferredWidth: 28
                    Text {
                        anchors.fill: parent
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 10
                        text: "\uf104"
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        color: MaterialDesignStyling.onPrimary
                    }
                    onClicked: {
                        root.model.goPrev();
                    }
                }

                Button {
                    height: navigation.height - 4
                    Layout.preferredWidth: 28
                    width: height
                    Text {
                        anchors.fill: parent
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 10
                        text: "\uf783"
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        color: MaterialDesignStyling.onPrimary
                    }

                    onClicked: {
                        root.model.goToday();
                    }
                }

                Button {
                    height: navigation.height - 4
                    Layout.preferredWidth: 28
                    width: height
                    Text {
                        anchors.fill: parent
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 10
                        text: "\uf200"
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        color: MaterialDesignStyling.onPrimary
                    }

                    onClicked: {
                        Common.openDialog("calendar/CategoryUsedPopup.qml", root, {
                                              model: root.model.getCategoryUseModel()
                                          });
                    }
                }


                Button {
                    height: navigation.height - 4
                    Layout.preferredWidth: 28
                    width: height
                    Text {
                        anchors.fill: parent
                        font.family: ce.faSolidName
                        font.styleName: ce.faSolidStyle
                        font.pixelSize: 10
                        text: "\uf105"
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        color: MaterialDesignStyling.onPrimary
                    }

                    onClicked: {
                        root.model.goNext();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

            }
        }

        Rectangle {
            id: header
            Layout.fillWidth: true
            height: 28
            color: MaterialDesignStyling.primaryContainer

            Canvas {
                id: canvasCtl
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.beginPath();
                    ctx.strokeStyle = MaterialDesignStyling.outline;
                    ctx.lineWidth = 4;

                    for(var i = 1; i < root.days; i++) {
                        var x = scrollView.dayWidth * i + hbar.width + (i * 4) - (i ? 4 : 0) + 2
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, header.height)
                    }
                    ctx.stroke();
                }
            }

            Repeater {
                model: root.days
                Label {
                    id: label
                    color: MaterialDesignStyling.onPrimaryContainer
                    x: scrollView.dayWidth * index + hbar.width + (index * 4) + 6
                    width: scrollView.dayWidth
                    height: header.height
                    //text: root.model.valid ? root.model.getDateStr(index) : "unset"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    Binding {
                        target: label
                        property: "text"
                        value: root.model.valid ? root.model.getDateStr(index) : "unset"
                        when: root.model.valid
                    }
                }
            }
        }

        // One day plannin
        ScrollView {
            id: scrollView
            property int hdrHeiht: 28
            property real dayHeight: root.hourHeight * 24.0
            property real dayWidth: (width - hbar.width - MaterialDesignStyling.scrollBarWidth - (4 * root.days - 1)) / root.days
            Layout.fillHeight: true
            Layout.fillWidth: true
            contentWidth: width - 10
            contentHeight: dayHeight

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            //ScrollBar.vertical.policy: ScrollBar.AlwaysOn
            //ScrollBar.vertical.size: MaterialDesignStyling.scrollBarWidth

            ScrollBar.vertical: ScrollBar {
                id: vScrollBar
                parent: scrollView
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: MaterialDesignStyling.scrollBarWidth
                policy: ScrollBar.AlwaysOn
            }

            Component.onCompleted: {
                scrollView.ScrollBar.vertical.position = 0.25
            }

            Rectangle {
                id: dayplan
                width: parent.width - MaterialDesignStyling.scrollBarWidth
                height: parent.height
                //spacing: 0
                color: MaterialDesignStyling.primaryContainer
                HoursBar {
                    id: hbar
                    x: 0
                    width: 48
                    height: parent.height
                    hourHeight: root.hourHeight
                    visible: true
                }

                Repeater {
                    onModelChanged: {
                        // console.log("Repeater model changed")
                    }

                    id: repeaterCtl
                    //property real dayWidth: (parent.width - hbar.width - (4 * root.days - 1)) / root.days
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    model: root.days
                    DayPlan {
                        id: dayplanCtl
                        x: hbar.width + (index * 4 ) + scrollView.dayWidth * index
                        width: scrollView.dayWidth
                        height: dayplan.height
                        hourHeight: root.hourHeight
                        model: root.model.getDayModel(dayplanCtl, index)
                        scroller: scrollView

                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    CommonElements {
        id: ce
    }
}
