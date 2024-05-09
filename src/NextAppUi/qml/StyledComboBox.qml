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
    id: control

    delegate: ItemDelegate {
        id: delegate

        required property var model
        required property int index
        highlighted: control.highlightedIndex === index

        width: control.width
        contentItem: Text {
            text: delegate.model[control.textRole]
            color: delegate.highlighted ? "black" : MaterialDesignStyling.onSecondaryContainer
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    indicator: Canvas {
        id: canvas
        x: control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 12
        height: 8
        contextType: "2d"

        Connections {
            target: control
            function onPressedChanged() { canvas.requestPaint(); }
        }

        onPaint: {
            context.reset();
            context.moveTo(0, 0);
            context.lineTo(width, 0);
            context.lineTo(width / 2, height);
            context.closePath();
            context.fillStyle = control.pressed ? MaterialDesignStyling.onPrimaryFixedVariant: MaterialDesignStyling.onPrimaryContainer
            context.fill();
        }
    }

    contentItem: Text {
        leftPadding: 6
        rightPadding: control.indicator.width + control.spacing

        text: control.displayText
        font: control.font
        color: control.pressed ? MaterialDesignStyling.onPrimaryFixedVariant: MaterialDesignStyling.onPrimaryContainer
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: MaterialDesignStyling.primaryContainer
        implicitWidth: 120
        implicitHeight: 40
        border.color: control.pressed ? MaterialDesignStyling.outline : MaterialDesignStyling.outlineVariant
        border.width: control.visualFocus ? 2 : 1
        radius: 2
    }

    popup: Popup {
        y: control.height - 1
        width: control.width
        implicitHeight: contentItem.implicitHeight
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            border.color: MaterialDesignStyling.outline
            radius: 2
            color: MaterialDesignStyling.secondaryContainer
        }
    }
}
