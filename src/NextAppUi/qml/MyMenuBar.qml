// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
// Copyright 2024 Jarle Aase and The Last Viking LTD

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import NextAppUi
import Nextapp.Models

MenuBar {
    id: root

    required property ApplicationWindow dragWindow
    property alias infoText: windowInfo.text

    // Customization of the top level menus inside the MenuBar
    delegate: MenuBarItem {
        id: menuBarItem

        contentItem: Text {
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter

            text: menuBarItem.text
            font: menuBarItem.font
            elide: Text.ElideRight
            color: menuBarItem.highlighted ? MaterialDesignStyling.inverseOnSurface : MaterialDesignStyling.onSurface
            opacity: enabled ? 1.0 : 0.3
        }

        background: Rectangle {
            id: background

            color: menuBarItem.highlighted ? MaterialDesignStyling.inverseSurface : MaterialDesignStyling.surface
            Rectangle {
                id: indicator

                width: 0; height: 3
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom

                color: MaterialDesignStyling.tertiaryFixedDim
                states: State {
                    name: "active"
                    when: menuBarItem.highlighted
                    PropertyChanges {
                        indicator.width: background.width - 2
                    }
                }
                transitions: Transition {
                    NumberAnimation {
                        properties: "width"
                        duration: 175
                    }
                }
            }
        }
    }
    // We use the contentItem property as a place to attach our window decorations. Beneath
    // the usual menu entries within a MenuBar, it includes a centered information text, along
    // with the minimize, maximize, and close buttons.
    contentItem: RowLayout {
        id: windowBar

        Layout.fillWidth: true
        Layout.fillHeight: true

        spacing: root.spacing
        Repeater {
            id: menuBarItems

            Layout.alignment: Qt.AlignLeft
            model: root.contentModel
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                id: windowInfo

                width: parent.width; height: parent.height
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                leftPadding: windowActions.width
                color: MaterialDesignStyling.onSurface
                clip: true
            }
        }

        RowLayout {
            id: windowActions
            visible: false

            Layout.alignment: Qt.AlignRight
            Layout.fillHeight: true

            spacing: 0

            component InteractionButton: Rectangle {
                id: interactionButton

                signal action()
                property alias hovered: hoverHandler.hovered

                Layout.fillHeight: true
                Layout.preferredWidth: height

                color: hovered ? MaterialDesignStyling.surfaceDim : "transparent"
                HoverHandler {
                    id: hoverHandler
                }
                TapHandler {
                    id: tapHandler
                    onTapped: interactionButton.action()
                }
            }

            InteractionButton {
                id: minimize

                onAction: root.dragWindow.showMinimized()
                Rectangle {
                    anchors.centerIn: parent
                    color: parent.hovered ? MaterialDesignStyling.surfaceBright : MaterialDesignStyling.surfaceContainer
                    height: 2
                    width: parent.height - 14
                }
            }

            InteractionButton {
                id: maximize

                onAction: root.dragWindow.showMaximized()
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 7
                    border.color: parent.hovered ? MaterialDesignStyling.tertiaryContainer : MaterialDesignStyling.tertiaryFixedDim
                    border.width: 2
                    color: "transparent"
                }
            }

            InteractionButton {
                id: close

                color: hovered ? "green" : "transparent"
                onAction: root.dragWindow.close()
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.height - 8; height: 2

                    rotation: 45
                    antialiasing: true
                    transformOrigin: Item.Center
                    color: parent.hovered ? "red" : "blue"

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.height
                        height: parent.width

                        antialiasing: true
                        color: parent.color
                    }
                }
            }
        }
    }

    background: Rectangle {
        color: NaCore.develBuild ? "red" : MaterialDesignStyling.surfaceContainer
        WindowDragHandler {
            enabled: Qt.platform.os !== "android" && Qt.platform.os !== "ios"
            dragWindow: enabled ? root.dragWindow : null
        }
    }
}
