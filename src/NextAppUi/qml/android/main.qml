import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models
import "../common.js" as Common


ApplicationWindow {
    id: root
    visible: true
    width: Screen.width
    height: Screen.height

    // Toolbar
    header: ToolBar {
        width: parent.width
        RowLayout {
            anchors.fill: parent
            ToolButton {
                icon.source: sidebar.currentIcon
                onClicked: drawer.open()
            }
            ToolButton {
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/bars.svg"
                onClicked: menuDrawer.open()
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Next-App")
                Layout.alignment: Qt.AlignCenter
            }

            Text {
                id: cloudIcon
                text: "\uf0c2"
                font.family: ce.faSolidName
                font.styleName: ce.faSolidStyle
                color: NaComm.connected ? "green" : "lightgray"
            }
        }
    }

    // Drawer Navigation
    Drawer {
        id: drawer
        width: Math.max(0.4 * parent.width, 100)
        height: parent.height

        DrawerContent {
            id: sidebar
            anchors.fill: parent
        }
    }

    Drawer {
        id: menuDrawer
        width: Math.max(0.6 * parent.width, 100)
        height: parent.height

        ColumnLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Menu")
                Layout.alignment: Qt.AlignCenter
            }

            Button {
                text: qsTr("Settings")
                onClicked: Common.openDialog("qrc:/qt/qml/NextAppUi/qml/settings/SettingsDlg.qml", root, {});
            }

            Button {
                text: NaComm.connected ? qsTr("Disconnect") : qsTr("Connect")
                onClicked: NaComm.toggleConnect()
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }

    // Main Content
    StackLayout {
        id: stackView
        currentIndex: sidebar.current
        anchors.fill: parent

        // ScrollView {
        //     ScrollBar.horizontal.policy: ScrollBar.AlwaysOn
        //     ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        //     contentHeight: daysInYear.implicitHeight
        //     contentWidth: daysInYear.implictWidth
        //     DaysInYear {
        //         id: daysInYear
        //     }
        // }

        DaysInYear {
            id: daysInYear
        }


        Rectangle {
            id: mainTreePage
            color: "yellow"
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                anchors.fill: parent
                MainTree {
                    id: mainTree
                    color: MaterialDesignStyling.surface
                }
            }
        }
    }

    CommonElements {
        id: ce
    }
}
