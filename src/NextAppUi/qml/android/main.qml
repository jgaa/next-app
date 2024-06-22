import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import NextAppUi
import Nextapp.Models
import "../common.js" as Common


ApplicationWindow {
    id: appWindow
    visible: true
    width: NaCore.isMobileSimulation ? 350 : Screen.width
    height: NaCore.isMobileSimulation ? 750 : Screen.height
    // width: 350
    // height: 750

    // Toolbar
    header: ToolBar {
        width: parent.width
        RowLayout {
            anchors.fill: parent
            // ToolButton {
            //     icon.source: sidebar.currentIcon
            //     onClicked: drawer.open()
            // }
            ToolButton {
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/bars.svg"
                onClicked: drawer.open()
            }

            Image {
                id: selImage
                source: sidebar.currentIcon
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                sourceSize.width: 24
                sourceSize.height: 24
            }

            MultiEffect {
                source: selImage
                anchors.fill: selImage
                brightness: 0.9
                colorization: 1.0
                colorizationColor: MaterialDesignStyling.onPrimaryContainer
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
        width: 0.75 * parent.width
        height: parent.height

        DrawerContent {
            id: sidebar
            anchors.fill: parent
        }
    }

    // Main Content
    StackLayout {
        id: stackView
        currentIndex: sidebar.current
        anchors.fill: parent

        // Green days
        // DaysInYear {
        //     id: daysInYear
        // }

        // Home screen
        TodoList {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        MainTree {
            id: mainTree
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        WorkSessionsStacked {
            id: workSessionsStacked
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        CalendarView {
            id: calendarPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            mode: CalendarModel.CM_DAY
            days: 1
        }
    }

    CommonElements {
        id: ce
    }
}
