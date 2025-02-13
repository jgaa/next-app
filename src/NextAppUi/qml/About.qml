import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    title: "About NextApp"
    standardButtons: Dialog.Close
    x: NaCore.isMobile ? 0 : (ApplicationWindow.window.width - width) / 3
    y: NaCore.isMobile ? 0 : (ApplicationWindow.window.height - height) / 3
    width: Math.min(ApplicationWindow.window.width, 450)
    height: Math.min(ApplicationWindow.window.height - 10, 650)

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        Image {
            id: logo
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true

            source: "../icons/nextapp.svg"
            sourceSize.width: 80
            sourceSize.height: 80
            fillMode: Image.PreserveAspectFit

            smooth: true
            antialiasing: true
            asynchronous: true
        }

        ScrollView {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.fillHeight: true

          TextArea {
              horizontalAlignment: Text.AlignHCenter
              textFormat: Text.RichText

              text: qsTr("<h3>About Nextapp</h3>"
                         + "<p>This is <i>Nextapp</i> version %1.</p>"
                         + "<p>Nextapp is a <i>Personal Organizer</i> app using the well-known <b>\"Getting Things Done\"</b> "
                         + "system described by David Allen in his famous book of the same name. "
                         + "Note that Nextapp is not affiliated with David Allen.</p>"
                         + "<p>Nextapp incorporates ideas for personal productivity and oversight from other "
                         + "sources, such as the free and open-source desktop application <b>WHID</b> (first released in 1998) "
                         + "and the free and open-source Android app <b>VikingGTD</b> (first released in 2013).</p>"
                         + "<p>In addition, we have added some gems to improve its usefulness, such as \"Green Days!\".</p>"
                         + "<p>The application is distributed under the <i>\"GNU GENERAL PUBLIC LICENSE v3.0\"</i>. Please see <a href=\"%2/\">%2</a> "
                         + "</p>"
                         + "<p>Copyright (C) 2023-%3 The Last Viking LTD, Jarle Aase, and other contributors.</p> "
                         + "This build of Nextapp uses QT version %4 (GPL versioning).<br/>"
                         + "The Nextapp server is version %5."
                         + "")
                       .arg(Application.version).arg("https://github.com/jgaa/next-app/blob/8dc49525facb8047137cd56f372afd5c6d2908fa/src/NextAppUi/LICENSE").arg("2025").arg(NaCore.qtVersion).arg(NaComm.version);
              wrapMode: Text.WordWrap
              readOnly: true
              antialiasing: true
              background: null

              onLinkActivated: function(link) {
                  Qt.openUrlExternally(link)
              }
          }
        }

        Item {
            Layout.preferredHeight: 10
        }
    }
}
