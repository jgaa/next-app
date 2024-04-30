import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

ApplicationWindow {
    id: root
    width: 1700
    height: 900
    visible: true
    title: NaCore.develBuild ? "nextapp --Developer Edition--" : qsTr("nextapp - Your Personal Organizer")
    color: Colors.background
    flags: Qt.Window //| Qt.FramelessWindowHint

    menuBar: MyMenuBar {
        dragWindow: root
        //infoText: root.getInfoText()
        MyMenu {
            title: qsTr("App")

            Action {
                text: qsTr("Settings")
                shortcut: StandardKey.ZoomIn
                onTriggered: { openDialog("SettingsDlg.qml") }
            }
            // Action {
            //     text: qsTr("Decrease Font")
            //     shortcut: StandardKey.ZoomOut
            //     onTriggered: editor.text.font.pixelSize -= 1
            // }
            // Action {
            //     text: root.showLineNumbers ? qsTr("Toggle Line Numbers OFF")
            //                                : qsTr("Toggle Line Numbers ON")
            //     shortcut: "Ctrl+L"
            //     onTriggered: root.showLineNumbers = !root.showLineNumbers
            // }
            // Action {
            //     text: root.expandPath ? qsTr("Toggle Short Path")
            //                           : qsTr("Toggle Expand Path")
            //     enabled: root.currentFilePath
            //     onTriggered: root.expandPath = !root.expandPath
            // }
            // Action {
            //     text: qsTr("Reset Filesystem")
            //     enabled: sidebar.currentTabIndex === 1
            //     onTriggered: fileSystemView.rootIndex = undefined
            // }
            Action {
                text: qsTr("Exit")
                onTriggered: Qt.exit(0)
                shortcut: StandardKey.Quit
            }
        }

        MyMenu {
            enabled: sidebar.currentMainItem == 1
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
            enabled: sidebar.currentMainItem == 1 && mainTree.hasSelection
            title: qsTr("Actions")

            Action {
                text: qsTr("New Action")
                onTriggered: openActionDlg()
            }
        }

        // MyMenu {
        //     title: qsTr("Edit")

        //     // Action {
        //     //     text: qsTr("Cut")
        //     //     shortcut: StandardKey.Cut
        //     //     enabled: editor.text.selectedText.length > 0
        //     //     onTriggered: editor.text.cut()
        //     // }
        //     // Action {
        //     //     text: qsTr("Copy")
        //     //     shortcut: StandardKey.Copy
        //     //     enabled: editor.text.selectedText.length > 0
        //     //     onTriggered: editor.text.copy()
        //     // }
        //     // Action {
        //     //     text: qsTr("Paste")
        //     //     shortcut: StandardKey.Paste
        //     //     enabled: editor.text.canPaste
        //     //     onTriggered: editor.text.paste()
        //     // }
        //     // Action {
        //     //     text: qsTr("Select All")
        //     //     shortcut: StandardKey.SelectAll
        //     //     enabled: editor.text.length > 0
        //     //     onTriggered: editor.text.selectAll()
        //     // }
        //     // Action {
        //     //     text: qsTr("Undo")
        //     //     shortcut: StandardKey.Undo
        //     //     enabled: editor.text.canUndo
        //     //     onTriggered: editor.text.undo()
        //     // }
        // }
    }


    RowLayout {
        // Green
        anchors.fill: parent

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
                        color: SplitHandle.pressed ? Colors.color2 : Colors.background
                        border.color: SplitHandle.hovered ? Colors.color2 : Colors.background
                        opacity: SplitHandle.hovered || navigationView.width < 15 ? 1.0 : 0.0

                        Behavior on opacity {
                            OpacityAnimator {
                                duration: 1400
                            }
                        }
                    }

                    Rectangle {
                        id: navigationView
                        color: Colors.surface1
                        SplitView.preferredWidth: 250

                        StackLayout {
                            anchors.fill: parent
                            //currentIndex: sidebar.currentTabIndex

                            MainTree {
                                id: mainTree
                                color: Colors.surface1
                                //onFileClicked: path => root.currentFilePath = path
                            }
                        }
                    }

                    Rectangle {
                        // Data
                        color: 'grey'
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

        } // StackLayout
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
