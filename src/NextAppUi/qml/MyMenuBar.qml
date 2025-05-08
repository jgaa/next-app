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

            Layout.fillWidth: true
            Layout.fillHeight: true


            ToolButton {
                text: "Gakk!"
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
