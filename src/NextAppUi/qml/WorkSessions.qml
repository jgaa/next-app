import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import NextAppUi
import nextapp.pb as NextappPB

Rectangle {
    color: Colors.background

    RowLayout {
        anchors.fill: parent
        id: row

        // List of work sessions
        TableView {
            model: WorkSessionsModel
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            clip: true
            Layout.fillHeight: true

            columnWidthProvider : function (column) {
                return 200;
            }

            delegate : Rectangle {
                Text {
                    text: display
                }
            }
        }

        // Right buttons
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 100

            Button {
                text: "New"
            }

            Button {
                text: "Pause"
            }

            Button {
                text: "Resume"
            }

            Button {
                text: "Done"
            }
        }
    }
}
