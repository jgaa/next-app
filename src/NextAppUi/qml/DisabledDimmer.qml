import QtQuick
import NextAppUi
import Nextapp.Models

// Component that dims the parent when it is disabled.
// Made to visually indicate that the app is offline by dimming out main surfaces.
Rectangle {
    id: dimmer
    anchors.fill: parent
    color: MaterialDesignStyling.inverseSurface
    z: 1000
    visible: !enabled
    opacity: 0.5
}
