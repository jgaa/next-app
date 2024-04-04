// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import NextAppUi

Rectangle {
    id: root

    //property alias currentTabIndex: topBar.currentIndex
    property int currentMainItem: 0
    property int currentTabIndex: -1
    required property ApplicationWindow dragWindow
    readonly property int tabBarSpacing: 10

    color: Colors.surface2

    component SidebarEntry: Button {
        id: sidebarButton

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true

        icon.color: down || checked ? Colors.iconIndicator : Colors.icon
        icon.width: 27
        icon.height: 27

        topPadding: 0
        rightPadding: 0
        bottomPadding: 0
        leftPadding: 0
        background: null

        Rectangle {
            id: indicator

            anchors.verticalCenter: parent.verticalCenter
            x: 2
            width: 4
            height: sidebarButton.icon.height * 1.2

            visible: sidebarButton.checked
            color: Colors.color1
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
        TabBar {
            id: topBar

            spacing: root.tabBarSpacing
            SidebarEntry {
                id: dayInYear
                icon.source: "../icons/fontawsome/calendar-days.svg"
                checkable: true
                checked: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 0
                        root.currentTabIndex = -1
                    }
                }
            }

            SidebarEntry {
                id: lists

                icon.source: "../icons/read.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 1
                        root.currentTabIndex = 0
                    }
                }
            }

            SidebarEntry {
                id: workHours

                icon.source: "../icons/fontawsome/clock.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 1
                        root.currentTabIndex = 1
                    }
                }
            }
        }

        // This item acts as a spacer to expand between the checkable and non-checkable buttons.
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            // Make the empty space drag our main window.
            WindowDragHandler {
                dragWindow: root.dragWindow
            }
        }

        TabBar {
            id: bottomBar

            spacing: root.tabBarSpacing
            // Opens the Qt website in the system's web browser.
            SidebarEntry {
                id: qtWebsiteButton
                icon.source: "../icons/globe.svg"
                checkable: false
                onClicked: Qt.openUrlExternally("https://github.com/jgaa/next-app")
            }

            // Opens the About Qt Window.
            SidebarEntry {
                id: aboutQtButton

                icon.source: "../icons/info_sign.svg"
                checkable: false
                onClicked: aboutQtWindow.visible = !aboutQtWindow.visible
            }
        }

        Rectangle {
            height: 2
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            color: Colors.text
        }

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
                    color: ServerComm.connected ? "green" : "lightgray"
                }
                Text {
                    //leftPadding: 5
                    text: ServerComm.connected ? qsTr("Online") : qsTr("Offline")
                    color: Colors.text
                }
            }

            Text {
                Layout.fillWidth: true
                visible: ServerComm.connected
                text: qsTr("Server") + " v" + ServerComm.version
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
}
