// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import NextAppUi
import Nextapp.Models 1.0

Rectangle {
    id: root

    //property alias currentTabIndex: topBar.currentIndex
    property int currentMainItem: 0
    property int currentTabIndex: -1
    property ApplicationWindow dragWindow: null
    readonly property int tabBarSpacing: 10
    property string currentIcon: topBar.currentItem.icon.source

    color: MaterialDesignStyling.surfaceContainer

    component SidebarEntry: Button {
        id: sidebarButton

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true

        icon.color: down || checked ? MaterialDesignStyling.onPrimaryContainer  : MaterialDesignStyling.onSurfaceVariant
        icon.width: 27
        icon.height: 27

        topPadding: 0
        rightPadding: 0
        bottomPadding: 0
        leftPadding: 0
        background: Rectangle {
            height: icon.height * 1.2
            x: 20
            width: parent.width - 40
            color: MaterialDesignStyling.onSurface
            opacity: sidebarButton.hovered ? 0.5 : 0
            radius: 5
        }

        hoverEnabled: true

        Rectangle {
            id: indicator

            anchors.verticalCenter: parent.verticalCenter
            x: 2
            width: 4
            height: sidebarButton.icon.height * 1.2

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
        TabBar {
            id: topBar

            spacing: root.tabBarSpacing
            SidebarEntry {
                id: dayInYear
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome//calendar-days.svg"
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

                icon.source: "qrc:/qt/qml/NextAppUi/icons/read.svg"
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

                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/clock.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 1
                        root.currentTabIndex = 1
                    }
                }
            }

            SidebarEntry {
                id: planDay

                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/calendar-day.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 2
                        root.currentTabIndex = -1
                    }
                }
            }

            SidebarEntry {
                id: reports

                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/chart-line.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 1
                        root.currentTabIndex = 2
                    }
                }
            }

            SidebarEntry {
                id: weeklyReview

                icon.source: "qrc:/qt/qml/NextAppUi/icons/weekly_review.svg"
                checkable: true

                onCheckedChanged: {
                    if (checked) {
                        root.currentMainItem = 1
                        root.currentTabIndex = 3
                    }
                }
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
                onClicked: Qt.openUrlExternally("https://next-app.org")
            }

            // Opens the About Qt Window.
            SidebarEntry {
                id: aboutQtButton

                icon.source: "qrc:/qt/qml/NextAppUi/icons/info_sign.svg"
                checkable: false
                onClicked: aboutQtWindow.visible = !aboutQtWindow.visible
            }
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
                    color: NaComm.statusColor
                }
                Text {
                    //leftPadding: 5
                    text: NaComm.statusText
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
}
