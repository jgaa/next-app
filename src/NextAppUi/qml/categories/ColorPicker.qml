import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

// QT don't have a color picker that works on all platforms, so we have to create our own
Dialog {
    id: popup
    // x: Math.min(Math.max(0, (parent.width - width) / 3), parent.width - width)
    // y: Math.min(Math.max(0, (parent.height - height) / 3), parent.height - height)
    x: 0
    y: 0
    width: Math.min(600, NaCore.width, Screen.width, parent.width)
    height:Math.min(600, NaCore.height, Screen.height, parent.height)
    modal: true
    standardButtons: Dialog.Close
    property string color: "blue"
    signal colorSelectionChanged(string color)

    background: Rectangle {
        color: MaterialDesignStyling.secondary
    }

    contentItem: ColumnLayout {
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            contentWidth: grid.implicitWidth
            contentHeight: grid.implicitHeight

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                id: grid
                columns: NaCore.width > 500 ? 4 : NaCore.width > 250 ? 2 : 1
                Repeater {
                    model: ["aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black",
                        "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse",
                        "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue",
                        "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki",
                        "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon",
                        "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise",
                        "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick",
                        "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod",
                        "gray", "green", "greenyellow", "grey", "honeydew", "hotpink", "indianred", "indigo",
                        "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue",
                        "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey",
                        "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray",
                        "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen",
                        "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple",
                        "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
                        "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite",
                        "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod",
                        "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink",
                        "plum", "powderblue", "purple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon",
                        "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue",
                        "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle",
                        "tomato", "turquoise", "violet", "wheat", "white",
                        "whitesmoke", "yellow", "yellowgreen"]

                    RoundButton {
                        background: Rectangle {
                            color: modelData
                            radius: 5
                        }
                        text: modelData
                        onClicked: {
                            popup.color = modelData
                            popup.close()
                            popup.colorSelectionChanged(modelData)
                        }
                    }
                }
            }
        }
        // Button {
        //     contentItem: Text {
        //             text:  qsTr("Close")
        //             color: MaterialDesignStyling.secondary
        //             horizontalAlignment: Text.AlignHCenter
        //             verticalAlignment: Text.AlignVCenter
        //         }
        //     onClicked: popup.close()
        //     background: Rectangle {
        //         color: MaterialDesignStyling.onSecondary
        //     }
        // }
    }
}
