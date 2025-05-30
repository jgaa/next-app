import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtCore
import NextAppUi
import Nextapp.Models 1.0
import nextapp.pb as NextappPb

pragma ComponentBehavior: Bound

ComboBox {
    id: root
    property int maxWidth: 0
    property int idealWidth: 120

    delegate: ItemDelegate {
        id: delegate

        required property var model
        required property int index
        highlighted: root.highlightedIndex === index

        width: root.width
        contentItem: Text {
            text: delegate.model[root.textRole]
            color: delegate.highlighted ? "black" : MaterialDesignStyling.onSecondaryContainer
            font: root.font
            //elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    TextMetrics {
        id: metrics
        font: root.font
        // NOTE: don't set `text` here, we'll do it dynamically
    }

    Component.onCompleted:   updateWidth()
    onModelChanged:         updateWidth()
    onCountChanged:         updateWidth()

    function updateWidth() {
            var maxW = 0
            for (var i = 0; i < root.count; ++i) {
                var s = root.model.get(i).text
                metrics.text = s
                maxW = Math.max(maxW, metrics.width)
            }
            // arrow icon + padding: tweak these constants to match your style
            var arrowAndPadding = 40 //root.indicator ? root.indicator.implicitWidth + 16 : 24

            idealWidth = Math.ceil(maxW + arrowAndPadding)
            if (root.maxWidth > 0) {
                idealWidth = Math.min(root.maxWidth, idealWidth)
            } else {
                root.implicitWidth = Math.ceil(maxW + arrowAndPadding)
            }
        }

    indicator: Canvas {
        id: canvas
        x: root.width - width - root.rightPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: 8
        contextType: "2d"

        Connections {
            target: root
            function onPressedChanged() { canvas.requestPaint(); }
        }

        onPaint: {
            context.reset();
            context.moveTo(0, 0);
            context.lineTo(width, 0);
            context.lineTo(width / 2, height);
            context.closePath();
            context.fillStyle = root.pressed ? MaterialDesignStyling.onPrimaryFixedVariant: MaterialDesignStyling.onPrimaryContainer
            context.fill();
        }
    }

    contentItem: Text {
        leftPadding: 6
        rightPadding: root.indicator.width + root.spacing

        text: root.displayText
        font: root.font
        color: root.pressed ? MaterialDesignStyling.onPrimaryFixedVariant: MaterialDesignStyling.onPrimaryContainer
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: MaterialDesignStyling.primaryContainer
        implicitWidth: root.implicitWidth  // 120
        implicitHeight: 40
        border.color: root.pressed ? MaterialDesignStyling.outline : MaterialDesignStyling.outlineVariant
        border.width: root.visualFocus ? 2 : 1
        radius: 2
    }

    popup: Popup {
        y: root.height - 1
        width: Math.max(root.idealWidth, 120)
        implicitHeight: contentItem.implicitHeight
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            border.color: MaterialDesignStyling.outline
            radius: 2
            color: MaterialDesignStyling.secondaryContainer
        }
    }
}
