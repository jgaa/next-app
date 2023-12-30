import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi

ApplicationWindow {
    id: root
    width: 650
    height: 550
    flags: Qt.Window // | Qt.FramelessWindowHint
    color: Colors.surface1
    title: "About Nextapp"

    menuBar: MyMenuBar {
        id: menuBar
        visible: false

        dragWindow: root
        implicitHeight: 30
        infoText: "About Nextapp"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Image {
            id: logo

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20

            source: "../icons/nextapp.svg"
            sourceSize.width: 80
            sourceSize.height: 80
            fillMode: Image.PreserveAspectFit

            smooth: true
            antialiasing: true
            asynchronous: true
        }

        ScrollView {
          anchors.margins: 20
          anchors.left: parent.left
          anchors.right: parent.right

          TextArea {
              selectedTextColor: Colors.textFile
              selectionColor: Colors.selection
              horizontalAlignment: Text.AlignHCenter
              textFormat: Text.RichText

              text: qsTr("<h3>About Nextapp</h3>"
                       + "<p>This is <i>Nextapp</i> version %1.</p>"
                       + "<p>Nextapp is a <i>Personal Organizer</i> app using the well known <b>\"Getting Things Done\"</b> "
                       + "system described by David Allen in his famous book of the same name. "
                       + "Note that Nextapp is not affiliated with David Allen.</p>"
                       + "<p>Nextapp incorporates ideas for personal prodictivity and oversight from other "
                       + "surces, such as the free and open source desktop application <b>WHID</b> (first released in 1998) "
                       + "and the free and open source Android app <b>VikingGTD</b> (first released in 2013).</p>"
                       + "<p>In addition, we have added some gems to improve its usefulness, such as \"Green Days!\".</p>"
                       + "<p>The application is distributed under the <i>\"GNU GENERAL PUBLIC LICENSE v3.0\"</i>. Please see <a href=\"http://%2/\">%2</a> "
                       + "</p>"
                       + "<p>Copyright (C) %3 The Last Viking LTD, Jarle Aase and other contributors.</p> "
                       + "This build of Nextapp use QT version %4 (GPL versioning).<br/>"
                       + "The nextapp-server is version %5."
                       + "")
                       .arg(Application.version).arg("https://github.com/jgaa/next-app/blob/main/src/NextAppUi/LICENSE").arg("2023").arg(NextAppCore.qtVersion).arg(ServerComm.version);
              color: Colors.textFile
              wrapMode: Text.WordWrap
              readOnly: true
              antialiasing: true
              background: null

              onLinkActivated: function(link) {
                  Qt.openUrlExternally(link)
              }
          }
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Ok"
            onClicked: root.close()
        }
    }

    ResizeButton {
        resizeWindow: root
    }
}
