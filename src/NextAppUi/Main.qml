import QtQuick
import QtQuick.Layouts

Window {
    width: 640
    height: 480
    visible: true
    title: sc.version

    RowLayout {
        // Green
        anchors.fill: parent

        Rectangle {
            // LeftMenu
            color: 'teal'
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 50
            Layout.preferredWidth: 100
            Layout.maximumWidth: 300
            Layout.minimumHeight: 150
        }

        ColumnLayout {
            // Orange
            Layout.fillWidth: true

            RowLayout {
                // Blue
                MainTree {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 100
                    Layout.preferredWidth: 150
                    Layout.maximumWidth: 500
                    Layout.minimumHeight: 150
                }

                Rectangle {
                    // Data
                    color: 'grey'
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 100
                    Layout.preferredWidth: 600
                }
            }

            Rectangle {
                // Details
                color: 'yellow'
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 30
                Layout.preferredHeight: 80
            }
        }
    }
}
