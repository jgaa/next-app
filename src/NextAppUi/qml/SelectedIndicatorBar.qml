import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import NextAppUi

Rectangle {
    property bool selected: false

    Layout.margins: 2
    Layout.preferredWidth: 5
    Layout.fillHeight: true
    color: selected ? MaterialDesignStyling.tertiary : "transparent"
    radius: 2
}
