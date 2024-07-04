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
    width: NaCore.width
    height: NaCore.height
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

            ToolButton {
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/arrows-up-down-left-right.svg"
                checkable: true
                checked: NaCore.dragEnabled

                onClicked: {
                    NaCore.dragEnabled = checked
                }
            }

            // Image {
            //     id: selImage
            //     source: sidebar.currentIcon
            //     Layout.preferredWidth: 24
            //     Layout.preferredHeight: 24
            //     sourceSize.width: 24
            //     sourceSize.height: 24
            // }

            // MultiEffect {
            //     source: selImage
            //     anchors.fill: selImage
            //     brightness: 0.9
            //     colorization: 1.0
            //     colorizationColor: MaterialDesignStyling.onPrimaryContainer
            // }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("NextApp")
                Layout.alignment: Qt.AlignCenter
            }

            Text {
                id: cloudIcon
                text: "\uf0c2"
                font.family: ce.faSolidName
                font.styleName: ce.faSolidStyle
                color: NaComm.connected ? "green" : "red"
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

            Connections {
                target: sidebar
                onSelectionChanged: (left, right) => {
                    console.log("Selection changed B ", left, " ", right)
                    dualView.setViews(left, right)
                }
            }
        }
    }

    DualView {
        id: dualView
        anchors.fill: parent
    }

    CommonElements {
        id: ce
    }
}
