// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Effects
import NextAppUi
import Nextapp.Models
import "../common.js" as Common

Rectangle {
    id: root

    property bool initialized: false
    readonly property int tabBarSpacing: 10
    signal selectionChanged(int left, int right)

    color: MaterialDesignStyling.surfaceContainer

    onVisibleChanged: {
        console.log("DrawerContent Visible: ", visible)

        if (!visible) {
            root.selectionChanged(
                        getCheckedButtonIndex("left", buttonGroupLeft),
                        getCheckedButtonIndex("right", buttonGroupRight))
        }
    }

    component SidebarEntry: Button {
        id: sidebarButton
        property int inconSize: 27
        property string bgColor: MaterialDesignStyling.onSurface

        Layout.alignment: Qt.AlignLeft
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10

        icon.color: down || checked ? MaterialDesignStyling.onPrimaryContainer  : MaterialDesignStyling.onSurfaceVariant
        icon.width: inconSize
        icon.height: inconSize

        // topPadding: 0
        // rightPadding: 0
        // bottomPadding: 0
        // leftPadding: 0
        background: Rectangle {
            height: icon.height * 1.2
            x: 20
            width: parent.width - 40
            color: sidebarButton.bgColor
            opacity: sidebarButton.hovered ? 0.5 : 0
            radius: 5
        }

        hoverEnabled: true

        Rectangle {
            id: indicator

            anchors.verticalCenter: parent.verticalCenter
            x: 2
            width: 6
            radius: 5
            height: sidebarButton.icon.height

            visible: sidebarButton.checked
            color: MaterialDesignStyling.onPrimaryContainer
        }
    }

    // TabBar is designed to be horizontal, whereas we need a vertical bar.
    // We can easily achieve that by using a Container.
    component TabBar: Container {
        id: tabBarComponent

        Layout.fillWidth: true
        // ButtonGroup ensures that only one button can be checked at a time.
        ButtonGroup {
            buttons: tabBarComponent.contentChildren

            // We have to manage the currentIndex ourselves, which we do by setting it to the index
            // of the currently checked button. We use setCurrentIndex instead of setting the
            // currentIndex property to avoid breaking bindings. See "Managing the Current Index"
            // in Container's documentation for more information.
            onCheckedButtonChanged: tabBarComponent.setCurrentIndex(
                Math.max(0, buttons.indexOf(checkedButton)))
        }

        contentItem: ColumnLayout {
            spacing: tabBarComponent.spacing
            Repeater {
                model: tabBarComponent.contentModel
            }
        }
    }

    ColumnLayout {
        anchors.fill: root
        anchors.topMargin: root.tabBarSpacing
        anchors.bottomMargin: root.tabBarSpacing
        spacing: root.tabBarSpacing

        ColumnLayout {
            id: selectionsBar
            Layout.margins: 10
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            property int colWidth: (root.width - 40) / 3

            ButtonGroup {
                id: buttonGroupLeft
            }

            ButtonGroup {
                id: buttonGroupRight
            }

            Repeater {
                id: repeaterCtl
                property var icons: [
                    "qrc:/qt/qml/NextAppUi/icons/fontawsome/list-check.svg",
                    "qrc:/qt/qml/NextAppUi/icons/fontawsome/folder-tree.svg",
                    "qrc:/qt/qml/NextAppUi/icons/fontawsome/hourglass-half.svg",
                    "qrc:/qt/qml/NextAppUi/icons/fontawsome/calendar-day.svg"
                ]
                model: [qsTr("Todos"), qsTr("Lists"), qsTr("Current"), qsTr("Calendar")]

                RowLayout {
                    //Layout.fillWidth: true
                    //Layout.alignment: Qt.AlignCenter
                    // Label {
                    //     text: modelData
                    //     color: "yellow"
                    //     Layout.preferredWidth: selectionsBar.colWidth
                    // }

                    RoundButton {
                        property int viewId: index
                        icon.source: repeaterCtl.icons[index]
                        checkable: true
                        checked: index === 0
                        Layout.preferredWidth: selectionsBar.colWidth
                        ButtonGroup.group: buttonGroupLeft
                    }

                    RoundButton {
                        property int viewId: index
                        icon.source: repeaterCtl.icons[index]
                        checkable: true
                        checked: index === 0
                        Layout.preferredWidth: selectionsBar.colWidth
                        ButtonGroup.group: buttonGroupRight

                        onClicked: {
                            console.log("Clicked: ", index, "checked: ", checked)
                        }
                    }
                }
            }

            RowLayout {
                Item {
                    Layout.preferredWidth: selectionsBar.colWidth
                }

                RoundButton {
                    //icon.source: repeaterCtl.icons[index]
                    property int viewId: 4
                    text: "X"
                    checkable: true
                    checked: false
                    Layout.preferredWidth: selectionsBar.colWidth
                    ButtonGroup.group: buttonGroupRight
                }
            }

            Rectangle {
                anchors.fill: parent
                color: MaterialDesignStyling.inverseSurface
                opacity: 0.2
                radius: 10
            }
        }

        ColumnLayout {
            id: menuBar
            spacing: root.tabBarSpacing
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.fillWidth: true

            RoundButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/gear.svg"
                checkable: false
                text: qsTr("Settings")
                onClicked: Common.openDialog("qrc:/qt/qml/NextAppUi/qml/settings/SettingsDlg.qml", appWindow, {});
            }

            RoundButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/layer-group.svg"
                checkable: false
                text: qsTr("Categories")
                onClicked: Common.openDialog("qrc:/qt/qml/NextAppUi/qml/categories/CategoriesMgr.qml", appWindow, {});
            }

            RoundButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/laptop.svg"
                checkable: false
                text: qsTr("Devices")
                onClicked: Common.openDialog("qrc:/qt/qml/NextAppUi/qml/onboard/GetNewOtpForDevice.qml", appWindow, {});
            }

            RoundButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/file-lines.svg"
                checkable: false
                text: qsTr("Application log")
                onClicked: Common.openDialog("qrc:/qt/qml/NextAppUi/qml/log/LogDialog.qml", appWindow, {});
            }

            Action {
                text: qsTr("Application log")
                onTriggered: { openDialog("log/LogDialog.qml") }
            }

            RoundButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: NaComm.connected ? qsTr("Disconnect") : qsTr("Connect")
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/cloud-bolt.svg"
                checkable: false
                onClicked: NaComm.toggleConnect()
            }
        }

        // This item acts as a spacer to expand between the checkable and non-checkable buttons.
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            // // Make the empty space drag our main window.
            // WindowDragHandler {
            //     dragWindow: root.dragWindow
            //     enabled: root.dragWindow !== null
            // }
        }

        TabBar {
            id: bottomBar

            spacing: root.tabBarSpacing
            // Opens the Qt website in the system's web browser.
            SidebarEntry {
                id: qtWebsiteButton
                icon.source: "qrc:/qt/qml/NextAppUi/icons/globe.svg"
                checkable: false
                onClicked: Qt.openUrlExternally("https://github.com/jgaa/next-app")
            }

            // Opens the About Qt Window.
            SidebarEntry {
                id: aboutQtButton

                icon.source: "qrc:/qt/qml/NextAppUi/icons/info_sign.svg"
                checkable: false
                onClicked: aboutQtWindow.visible = !aboutQtWindow.visible
            }
        }

        // Rectangle {
        //     height: 2
        //     Layout.fillWidth: true
        //     Layout.alignment: Qt.AlignHCenter
        //     color: Colors.text
        // }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Text {
                    id: cloudIcon
                    //height: 32
                    text: "\uf0c2"
                    font.family: ce.faSolidName
                    font.styleName: ce.faSolidStyle
                    font.pixelSize: 18
                    color: NaComm.connected ? "green" : "lightgray"
                }
                Text {
                    //leftPadding: 5
                    text: NaComm.connected ? qsTr("Online") : qsTr("Offline")
                    color: Colors.text
                }
            }

            Text {
                Layout.fillWidth: true
                visible: NaComm.connected
                text: qsTr("Server") + " v" + NaComm.version
                color: Colors.text
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    About {
        id: aboutQtWindow
        visible: false
    }

    CommonElements {
        id: ce
    }

    function getCheckedButtonIndex(name, buttonGroup) {
        if (buttonGroup.checkedButton) {
            console.log("Button ", buttonGroup.checkedButton.viewId, " checked in button group ", name)
            return buttonGroup.checkedButton.viewId
        }

        console.log("No button checked in button group ", name)
        return 4
    }
}
