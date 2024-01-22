import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi

ApplicationWindow {
    id: root
    width: 1700
    height: 900
    visible: true
    title: NextAppCore.develBuild ? "nextapp --Developer Edition--" : qsTr("nextapp - Your Personal Organizer")
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
                //     var component = Qt.createComponent("qml/SettingsDlg.qml");
                //     if (component.status !== Component.Ready) {
                //         if(component.status === Component.Error )
                //             console.debug("Error:"+ component.errorString() );
                //         return;
                //     }
                //     var dlg = component.createObject(root, {});
                //     dlg.open()
                // }
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
                onTriggered: {

                    console.log("parent: ", mainTree.currentIndex);

                    openDialog("EditNodeDlg.qml", {
                    isNew: true,
                    parentIx: mainTree.currentIndex,
                    type: "folder",
                    title: qsTr("New Folder")
                })}
            }
            Action {
                text: qsTr("New Customer")
                //onTriggered: editor.text.copy()
            }
            Action {
                text: qsTr("New Project")
                //onTriggered: editor.text.paste()
            }
            Action {
                text: qsTr("New List")
                //onTriggered: editor.text.selectAll()
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
            Layout.preferredWidth: 50
            Layout.fillHeight: true
        }

        StackLayout {
            //anchors.fill: parent
            currentIndex: sidebar.currentMainItem

            DaysInYear {}

            ColumnLayout {
                // Orange
                Layout.fillWidth: true

                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                        SplitView.fillHeight: true
                        // The stack-layout provides different views, based on the
                        // selected buttons inside the sidebar.
                        StackLayout {
                            anchors.fill: parent
                            currentIndex: sidebar.currentTabIndex

                            // // Shows the help text.
                            // Text {
                            //     text: qsTr("This example shows how to use and visualize the file system.\n\n"
                            //              + "Customized Qt Quick Components have been used to achieve this look.\n\n"
                            //              + "You can edit the files but they won't be changed on the file system.\n\n"
                            //              + "Click on the folder icon to the left to get started.")
                            //     wrapMode: TextArea.Wrap
                            //     color: Colors.text
                            // }

                            // Shows the files on the file system.
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
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 100
                        Layout.preferredWidth: 600
                    }
                }

                Rectangle {
                    height: 200
                    // Details
                    color: 'yellow'
                    Layout.fillWidth: true
                    //Layout.fillHeight: true
                    Layout.minimumHeight: 30
                    Layout.preferredHeight: 80
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
}
