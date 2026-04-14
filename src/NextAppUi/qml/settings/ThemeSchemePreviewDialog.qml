import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import NextAppUi
import Nextapp.Models

Dialog {
    id: root

    parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        margins: 12

    width: NaCore.isMobile
               ? Overlay.overlay.width
               : Math.min(Overlay.overlay.width - 24, 980)

    height: NaCore.isMobile
            ? Overlay.overlay.height
            : Math.min(Overlay.overlay.height - 24, 760)

    title: qsTr("Theme Scheme Preview")
    modal: true
    standardButtons: Dialog.Close
    padding: 0

    property var themeNames: MaterialDesignStyling.availableThemes()
    property int contentPadding: 18
    property var groupedRoles: [
        {
            title: qsTr("Key Colors"),
            roles: [
                "primary", "onPrimary",
                "primaryContainer", "onPrimaryContainer",
                "secondary", "onSecondary",
                "secondaryContainer", "onSecondaryContainer",
                "tertiary", "onTertiary",
                "tertiaryContainer", "onTertiaryContainer",
                "error", "onError",
                "errorContainer", "onErrorContainer"
            ]
        },
        {
            title: qsTr("Neutral Colors"),
            roles: [
                "surface", "onSurface",
                "surfaceVariant", "onSurfaceVariant",
                "background", "onBackground",
                "outline", "outlineVariant",
                "inverseSurface", "inverseOnSurface", "inversePrimary"
            ]
        },
        {
            title: qsTr("Fixed Roles"),
            roles: [
                "primaryFixed", "onPrimaryFixed", "primaryFixedDim", "onPrimaryFixedVariant",
                "secondaryFixed", "onSecondaryFixed", "secondaryFixedDim", "onSecondaryFixedVariant",
                "tertiaryFixed", "onTertiaryFixed", "tertiaryFixedDim", "onTertiaryFixedVariant"
            ]
        },
        {
            title: qsTr("Additional Surface Roles"),
            roles: [
                "surfaceDim", "surfaceBright",
                "surfaceContainerLowest", "surfaceContainerLow",
                "surfaceContainer", "surfaceContainerHigh", "surfaceContainerHighest",
                "scrim", "shadow"
            ]
        }
    ]

    function titleCase(name) {
        return name.length ? name.charAt(0).toUpperCase() + name.slice(1) : name
    }

    function pairRole(roleName) {
        const pairs = {
            primary: "onPrimary",
            onPrimary: "primary",
            primaryContainer: "onPrimaryContainer",
            onPrimaryContainer: "primaryContainer",
            primaryFixed: "onPrimaryFixed",
            onPrimaryFixed: "primaryFixed",
            primaryFixedDim: "onPrimaryFixedVariant",
            onPrimaryFixedVariant: "primaryFixedDim",
            secondary: "onSecondary",
            onSecondary: "secondary",
            secondaryContainer: "onSecondaryContainer",
            onSecondaryContainer: "secondaryContainer",
            secondaryFixed: "onSecondaryFixed",
            onSecondaryFixed: "secondaryFixed",
            secondaryFixedDim: "onSecondaryFixedVariant",
            onSecondaryFixedVariant: "secondaryFixedDim",
            tertiary: "onTertiary",
            onTertiary: "tertiary",
            tertiaryContainer: "onTertiaryContainer",
            onTertiaryContainer: "tertiaryContainer",
            tertiaryFixed: "onTertiaryFixed",
            onTertiaryFixed: "tertiaryFixed",
            tertiaryFixedDim: "onTertiaryFixedVariant",
            onTertiaryFixedVariant: "tertiaryFixedDim",
            error: "onError",
            onError: "error",
            errorContainer: "onErrorContainer",
            onErrorContainer: "errorContainer",
            surface: "onSurface",
            onSurface: "surface",
            surfaceVariant: "onSurfaceVariant",
            onSurfaceVariant: "surfaceVariant",
            background: "onBackground",
            onBackground: "background",
            inverseSurface: "inverseOnSurface",
            inverseOnSurface: "inverseSurface",
            inversePrimary: "inverseSurface",
            outline: "surface",
            outlineVariant: "surface",
            surfaceDim: "onSurface",
            surfaceBright: "onSurface",
            surfaceContainerLowest: "onSurface",
            surfaceContainerLow: "onSurface",
            surfaceContainer: "onSurface",
            surfaceContainerHigh: "onSurface",
            surfaceContainerHighest: "onSurface",
            scrim: "surface",
            shadow: "surface"
        }

        return pairs[roleName] || "onSurface"
    }

    function roleValue(themeData, roleName) {
        return themeData[roleName] || "#000000"
    }

    function normalizeHexColor(value) {
        if (!value || typeof value !== "string") {
            return "#000000"
        }

        if (value.length === 4) {
            return "#" + value[1] + value[1] + value[2] + value[2] + value[3] + value[3]
        }

        if (value.length === 7) {
            return value
        }

        if (value.length === 9) {
            return "#" + value.slice(3)
        }

        return "#000000"
    }

    function colorChannels(value) {
        const hex = normalizeHexColor(value)
        return {
            r: parseInt(hex.slice(1, 3), 16) / 255.0,
            g: parseInt(hex.slice(3, 5), 16) / 255.0,
            b: parseInt(hex.slice(5, 7), 16) / 255.0
        }
    }

    function linearizeChannel(channel) {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4)
    }

    function relativeLuminance(value) {
        const channels = colorChannels(value)
        const r = linearizeChannel(channels.r)
        const g = linearizeChannel(channels.g)
        const b = linearizeChannel(channels.b)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    }

    function contrastRatio(foreground, background) {
        const fg = relativeLuminance(foreground)
        const bg = relativeLuminance(background)
        const lighter = Math.max(fg, bg)
        const darker = Math.min(fg, bg)
        return (lighter + 0.05) / (darker + 0.05)
    }

    function roleTextColor(themeData, roleName) {
        const background = roleValue(themeData, roleName)
        const preferred = roleValue(themeData, pairRole(roleName))
        const preferredContrast = contrastRatio(preferred, background)

        if (preferredContrast >= 4.5) {
            return preferred
        }

        const whiteContrast = contrastRatio("#FFFFFF", background)
        const blackContrast = contrastRatio("#000000", background)
        return whiteContrast >= blackContrast ? "#FFFFFF" : "#000000"
    }

    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                color: MaterialDesignStyling.surfaceContainer

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 4

                    Label {
                        text: qsTr("Built-in Material Theme Schemes")
                        font.pixelSize: 20
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Each tab renders one built-in theme using the scheme roles defined in nextapp.")
                        wrapMode: Text.WordWrap
                        color: MaterialDesignStyling.onSurfaceVariant
                    }
                }
            }

            TabBar {
                id: tabBar
                Layout.fillWidth: true

                Repeater {
                    model: root.themeNames

                    TabButton {
                        required property string modelData
                        text: root.titleCase(modelData)
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabBar.currentIndex

                Repeater {
                    model: root.themeNames

                    Item {
                        id: themePage
                        required property string modelData
                        readonly property var themeData: MaterialDesignStyling.previewTheme(modelData)

                        ScrollView {
                            anchors.fill: parent
                            clip: true

                            contentWidth: availableWidth

                            Column {
                                id: scrollColumn
                                x: root.contentPadding
                                y: root.contentPadding
                                width: Math.max(0, parent.width - (root.contentPadding * 2))
                                spacing: 18

                                Repeater {
                                    model: root.groupedRoles

                                    Column {
                                        required property var modelData
                                        width: parent.width
                                        spacing: 10

                                        Label {
                                            text: modelData.title
                                            font.pixelSize: 18
                                            font.bold: true
                                        }

                                        GridLayout {
                                            width: parent.width
                                            columns: width > 760 ? 4 : width > 500 ? 3 : 2
                                            rowSpacing: 10
                                            columnSpacing: 10

                                            Repeater {
                                                model: modelData.roles

                                                Rectangle {
                                                    required property string modelData
                                                    readonly property color swatchColor: root.roleValue(themePage.themeData, modelData)
                                                    readonly property color textColor: root.roleTextColor(themePage.themeData, modelData)
                                                    readonly property color preferredTextColor: root.roleValue(themePage.themeData, root.pairRole(modelData))
                                                    readonly property bool usesOverrideTextColor: textColor.toString().toLowerCase() !== preferredTextColor.toString().toLowerCase()

                                                    Layout.fillWidth: true
                                                    Layout.preferredWidth: (parent.width - ((parent.columns - 1) * parent.columnSpacing)) / parent.columns
                                                    Layout.preferredHeight: 112
                                                    radius: 16
                                                    color: swatchColor
                                                    border.width: 1
                                                    border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.16)

                                                    ColumnLayout {
                                                        anchors.fill: parent
                                                        anchors.margins: 12
                                                        spacing: 4

                                                        TextInput {
                                                            readOnly: true
                                                            Layout.fillWidth: true
                                                            text: modelData
                                                            color: textColor
                                                            font.bold: true
                                                            //elide: Text.ElideRight
                                                        }

                                                        TextInput {
                                                            readOnly: true
                                                            Layout.fillWidth: true
                                                            text: root.roleValue(themePage.themeData, modelData)
                                                            color: textColor
                                                            opacity: 0.9
                                                            font.family: "monospace"
                                                            //elide: Text.ElideRight
                                                        }

                                                        Item { Layout.fillHeight: true }

                                                        TextInput {
                                                            visible: !parent.parent.usesOverrideTextColor
                                                            readOnly: true
                                                            Layout.fillWidth: true
                                                            text: qsTr("Text: %1").arg(root.pairRole(modelData))
                                                            color: textColor
                                                            opacity: 0.82
                                                            //elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
