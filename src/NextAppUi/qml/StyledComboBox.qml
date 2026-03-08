import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

pragma ComponentBehavior: Bound

Control {
    id: root
    property var model
    property string textRole: "text"
    property int currentIndex: -1
    property string currentText: textAt(currentIndex)
    property string displayText: currentIndex >= 0 ? currentText : ""
    property int maxWidth: 0
    property int idealWidth: 120
    property int popupIdealWidth: 120
    property int count: {
        if (!model) {
            return 0
        }
        if (typeof model.count === "number") {
            return model.count
        }
        if (typeof model.length === "number") {
            return model.length
        }
        return 0
    }
    property int implicitContentWidthPolicy: 0
    signal activated(int index)

    implicitWidth: Math.max(idealWidth, 120)
    implicitHeight: 40
    leftPadding: 6
    rightPadding: indicator.width + spacing + 6
    topPadding: 6
    bottomPadding: 6

    function itemAt(index) {
        if (!model || index < 0 || index >= count) {
            return null
        }
        if (typeof model.get === "function") {
            return model.get(index)
        }
        return model[index]
    }

    function textAt(index) {
        const item = itemAt(index)
        if (item === null || item === undefined) {
            return ""
        }
        if (typeof item === "string") {
            return item
        }
        const value = item[textRole]
        return value === undefined || value === null ? "" : value
    }

    function updateWidth() {
        var maxW = 0
        for (var i = 0; i < count; ++i) {
            metrics.text = textAt(i)
            maxW = Math.max(maxW, metrics.width)
        }

        var arrowAndPadding = 40
        popupIdealWidth = Math.ceil(maxW + arrowAndPadding)
        idealWidth = popupIdealWidth
        if (maxWidth > 0) {
            idealWidth = Math.min(maxWidth, idealWidth)
        }
    }

    function openPopup() {
        popup.open()
    }

    function closePopup() {
        popup.close()
    }

    onModelChanged: updateWidth()
    onCountChanged: updateWidth()
    onFontChanged: updateWidth()
    Component.onCompleted: updateWidth()

    TextMetrics {
        id: metrics
        font: root.font
    }

    background: Rectangle {
        color: MaterialDesignStyling.primaryContainer
        border.color: mouseArea.pressed ? MaterialDesignStyling.outline : MaterialDesignStyling.outlineVariant
        border.width: root.visualFocus ? 2 : 1
        radius: 2
    }

    contentItem: Text {
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        text: root.displayText
        font: root.font
        color: mouseArea.pressed ? MaterialDesignStyling.onPrimaryFixedVariant : MaterialDesignStyling.onPrimaryContainer
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Canvas {
        id: indicator
        x: root.width - width - 8
        y: (root.height - height) / 2
        width: 12
        height: 8
        contextType: "2d"

        Connections {
            target: mouseArea
            function onPressedChanged() { indicator.requestPaint() }
        }

        onPaint: {
            context.reset()
            context.moveTo(0, 0)
            context.lineTo(width, 0)
            context.lineTo(width / 2, height)
            context.closePath()
            context.fillStyle = mouseArea.pressed
                ? MaterialDesignStyling.onPrimaryFixedVariant
                : MaterialDesignStyling.onPrimaryContainer
            context.fill()
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (popup.opened) {
                popup.close()
            } else {
                popup.open()
            }
        }
    }

    Popup {
        id: popup
        y: root.height
        width: Math.max(root.width, Math.max(root.popupIdealWidth, 120))
        padding: 1
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: popup.visible ? root.count : 0

            ScrollIndicator.vertical: ScrollIndicator {}

            delegate: Rectangle {
                id: delegate
                required property int index
                property bool hovered: mouseArea.containsMouse
                property bool active: hovered || root.currentIndex === index

                width: ListView.view.width
                implicitHeight: 36
                color: active
                    ? MaterialDesignStyling.secondaryFixedDim
                    : MaterialDesignStyling.secondaryContainer

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: root.textAt(delegate.index)
                    color: delegate.active
                        ? MaterialDesignStyling.onSecondaryFixedVariant
                        : MaterialDesignStyling.onSecondaryContainer
                    font: root.font
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.currentIndex = delegate.index
                        root.activated(delegate.index)
                        popup.close()
                    }
                }
            }
        }

        background: Rectangle {
            border.color: MaterialDesignStyling.outline
            radius: 2
            color: MaterialDesignStyling.secondaryContainer
        }
    }
}
