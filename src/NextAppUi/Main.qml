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
    visible: settings.onboarding
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
        MyMenu {
            title: qsTr("App")

            Action {
                text: qsTr("Settings")
                shortcut: StandardKey.ZoomIn
                onTriggered: { openDialog("settings/SettingsDlg.qml") }
            }

            Action {
                text: qsTr("Categories")
                onTriggered: { openDialog("categories/CategoriesMgr.qml") }
                enabled: NaComm.connected
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

    SplitView {
        // Green

        anchors.fill: parent
        orientation: Qt.Horizontal

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

                DaysInYear {}

                SplitView {
                    // Orange
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    SplitView {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        orientation: Qt.Horizontal

                        // Customized handle to drag between the Navigation and the Editor.
                        handle: Rectangle {
                            implicitWidth: 10
                            color: SplitHandle.pressed ? MaterialDesignStyling.primary : MaterialDesignStyling.surfaceContainerHigh
                            border.color: SplitHandle.hovered ? MaterialDesignStyling.outline : MaterialDesignStyling.surfaceContainer
                            opacity: SplitHandle.hovered || navigationView.width < 15 ? 1.0 : 0.3

                            Behavior on opacity {
                                OpacityAnimator {
                                    duration: 1400
                                }
                            }
                        }

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

                                ActionsListView {}

                                WorkSessionsView {}

                                ReportsView {}
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
            }
        }
    } //Green SplitView

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
            aprx: ActionsModel.getAction("")
        });
    }

}
