import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models

ApplicationWindow {
    id: root
    width: 1700
    height: 900
    visible: NaComm.signupStatus == NaComm.SIGNUP_OK
    title: NaCore.develBuild ? "Next-app --Developer Edition--" : qsTr("Next-app: Your Personal Organizer")
    color: MaterialDesignStyling.surface
    flags: Qt.Window //| Qt.FramelessWindowHint

    Settings {
        id: settings
        property bool onboarding: false
    }

    Component.onCompleted: {
        if (!settings.onboarding) {
            console.log("Opening onboarding")
            openWindow("onboard/OnBoardingWizard.qml");
        }
    }

    menuBar: MyMenuBar {
        dragWindow: root
        //infoText: root.getInfoText()
        Menu {
            title: qsTr("App")

            Action {
                text: qsTr("Settings")
                shortcut: StandardKey.ZoomIn
                onTriggered: { openDialog("settings/SettingsDlg.qml") }
            }

            Action {
                text: qsTr("Devices")
                shortcut: StandardKey.ZoomIn
                //onTriggered: { openDialog("onboard/GetNewOtpForDevice.qml") }
                onTriggered: { openDialog("DevicesDlg.qml") }
            }

            Action {
                text: qsTr("Categories")
                onTriggered: { openDialog("categories/CategoriesMgr.qml") }
                enabled: NaComm.connected
            }

            Action {
                text: qsTr("Application log")
                onTriggered: { openDialog("log/LogDialog.qml") }
            }

            Action {
                text: qsTr("Exit")
                onTriggered: Qt.exit(0)
                shortcut: StandardKey.Quit
            }

            Action {
                text: NaComm.connected ? qsTr("Disconnect") : qsTr("Connect")
                onTriggered: NaComm.toggleConnect();
            }
        }

        MyMenu {
            enabled: sidebar.currentMainItem == 1 && NaComm.connected
            title: qsTr("Lists")

            Action {
                text: qsTr("New Folder")
                onTriggered: openNodeDlg("folder")
            }
            Action {
                text: qsTr("New Organization/Customer")
                onTriggered: openNodeDlg("organization")
            }
            Action {
                text: qsTr("New Person")
                onTriggered: openNodeDlg("person")
            }
            Action {
                text: qsTr("New Project")
                onTriggered: openNodeDlg("project")
            }
            Action {
                text: qsTr("New Task")
                onTriggered: openNodeDlg("task")
            }
        }

        MyMenu {
            enabled: sidebar.currentMainItem == 1 && mainTree.hasSelection && NaComm.connected
            title: qsTr("Actions")

            Action {
                text: qsTr("New Action")
                onTriggered: openActionDlg()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        SplitView {
            // Green
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: SplitterStyle {}

            RowLayout {
                // Purple
                //anchors.fill: parent
                SplitView.preferredWidth: root.width - 360
                SplitView.fillHeight: true
                //Layout.preferredWidth: root.width - dayPlan.implicitWidth - 10

                // Stores the buttons that navigate the application.
                Sidebar {
                    id: sidebar
                    dragWindow: root
                    Layout.preferredWidth: 100
                    Layout.fillHeight: true
                }

                StackLayout {
                    //anchors.fill: parent
                    currentIndex: sidebar.currentMainItem

                    DaysInYear {
                        // Layout.fillWidth: true
                        // Layout.fillHeight: true
                    }

                    SplitView {
                        // Orange
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Vertical
                        handle: SplitterStyle {
                            vertical: true
                        }

                        SplitView {
                            SplitView.fillWidth: true
                            SplitView.fillHeight: true
                            orientation: Qt.Horizontal
                            handle: SplitterStyle {}

                            Rectangle {
                                id: navigationView
                                color: "yellow"
                                SplitView.preferredWidth: 250

                                StackLayout {
                                    anchors.fill: parent
                                    MainTree {
                                        id: mainTree
                                        color: MaterialDesignStyling.surface
                                    }
                                }
                            }

                            Rectangle {
                                // Data
                                color: "red"
                                //SplitView.fillWidth: true
                                SplitView.fillHeight: true
                                SplitView.minimumWidth: 100
                                SplitView.preferredWidth: 600
                                StackLayout {
                                    id: currentData
                                    currentIndex: sidebar.currentTabIndex
                                    anchors.fill: parent

                                    ActionsListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                    }

                                    WorkSessionsView {}

                                    ReportsView {}

                                    WeeklyReview {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        navigation: mainTree
                                    }
                                }
                            }
                        }

                        CurrentWorkSessionsView {
                            SplitView.fillWidth: true
                            SplitView.fillHeight: false
                            SplitView.preferredHeight: 220
                        }
                    } // ColumnLayout for tree views

                    CalendarView {
                        id: calendarView
                        mode: CalendarModel.CM_WEEK
                        days: 7
                    }

                } // StackLayout
            } // Purple RowLayout

            ColumnLayout {
                SplitView.preferredWidth: 400
                //SplitView.maximumWidth: 360
                SplitView.minimumWidth: 150
                SplitView.fillHeight: true
                Layout.maximumWidth: dayPlan.implicitWidth

                CalendarView {
                    id: dayPlan
                    Layout.fillHeight: true
                    visible: sidebar.currentMainItem !== 2
                    mode: CalendarModel.CM_DAY
                    days: 1
                    primaryForActionList: true
                }
            }
        } //Green SplitView

        Rectangle {
            id: bottomBar
            Layout.fillWidth: true
            Layout.preferredHeight: root.menuBar.height
            color: MaterialDesignStyling.surfaceContainer

            RowLayout {
                anchors.fill: parent

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    Layout.preferredWidth: Math.max(parent.width * 0.3, 300)
                    Layout.minimumWidth: 300
                    Layout.maximumWidth: 500
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillHeight: true
                    text: NaLogModel.message
                    color: NaLogModel.messageColor
                    //font.bold: true
                    verticalAlignment: Text.AlignVCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor // Change cursor on hover
                        onClicked: {
                            openDialog("log/LogDialog.qml")
                        }
                    }

                    Rectangle {
                        // put it under the Text so the text is visible
                        z: -1
                        anchors.fill: parent
                        color: NaLogModel.message.length > 1 ? "white" : "transparent"
                        radius: 5
                    }
                }
            }
        }
    }

    ResizeButton {
        visible: false
        resizeWindow: root
    }

    function openDialog(name, args) {
        var component = Qt.createComponent("qml/" + name);
        if (component.status !== Component.Ready) {
            if(component.status === Component.Error )
                console.debug("Error:"+ component.errorString() );
            return;
        }
        var dlg = component.createObject(root, args);
        dlg.open()
    }

    function openWindow(name, args) {
        var component = Qt.createComponent("qml/" + name);
        if (component.status !== Component.Ready) {
            if(component.status === Component.Error )
                console.debug("Error:"+ component.errorString() );
            return;
        }
        var win = component.createObject(root, args);
        win.show()
    }

    function openNodeDlg(kind) {
        openDialog("EditNodeDlg.qml", {
            isNew: true,
            kind: kind,
            title: qsTr("New Folder")
        });
    }

    function openActionDlg(kind) {
        openDialog("EditActionDlg.qml", {
            node: mainTree.selectedItemUuid,
            title: qsTr("New Action"),
            aprx: NaActionsModel.getAction("")
        });
    }

}
