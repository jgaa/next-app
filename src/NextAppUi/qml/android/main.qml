import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import NextAppUi
import Nextapp.Models
import "../common.js" as Common


ApplicationWindow {
    id: appWindow
    visible: NaComm.signupStatus == NaComm.SIGNUP_OK
    width: NaCore.width
    height: NaCore.height

    Settings {
        id: settings
        property bool onboarding: false
    }

    Component.onCompleted: {
        if (!settings.onboarding) {
            console.log("Opening onboarding")
            openWindow("onboard/OnBoardingWizard.qml");
        }
    }

    Connections {
        target: NaCore
        function onImportEvent(url) {
            console.log("Got .nextapp to open:", url)
            confirmImport.fileUrl = url;
            confirmImport.open();
        }
    }

    MessageDialog {
        id: confirmImport
        property url fileUrl: ""

        title: qsTr("Danger Zone")
        text: qsTr("All your existing data will be replaced with the data from this file.\nThis action cannot be undone.\n\nAre you sure you want to continue?")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
           ImportExportModel.importData(fileUrl);
           confirmImport.close()
        }

        onRejected: {
            confirmImport.close()
        }
    }

    // Toolbar
    header: ToolBar {
        width: parent.width

        RowLayout {
            anchors.fill: parent
            // ToolButton {
            //     icon.source: sidebar.currentIcon
            //     onClicked: drawer.open()
            // }
            ToolButton {
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/bars.svg"
                onClicked: drawer.open()
            }

            ToolButton {
                icon.source: "qrc:/qt/qml/NextAppUi/icons/fontawsome/arrows-up-down-left-right.svg"
                checkable: true
                checked: NaCore.dragEnabled

                onClicked: {
                    NaCore.dragEnabled = checked
                }
            }

            // Image {
            //     id: selImage
            //     source: sidebar.currentIcon
            //     Layout.preferredWidth: 24
            //     Layout.preferredHeight: 24
            //     sourceSize.width: 24
            //     sourceSize.height: 24
            // }

            // MultiEffect {
            //     source: selImage
            //     anchors.fill: selImage
            //     brightness: 0.9
            //     colorization: 1.0
            //     colorizationColor: MaterialDesignStyling.onPrimaryContainer
            // }

            Item {
                Layout.fillWidth: true
            }

            Label {
                id: nextappLabelDebug
                text: qsTr("NextAppDbg")
                Layout.alignment: Qt.AlignCenter
                visible: !notificationIcon.visible && NaCore.isDebugBuild
                color: "pink"
            }

            Label {
                id: nextappLabel
                text: qsTr("NextApp")
                Layout.alignment: Qt.AlignCenter
                visible: !notificationIcon.visible && !NaCore.isDebugBuild
            }

            CheckBoxWithFontIcon {
                id: notificationIcon
                uncheckedCode: "\uf024"
                checkedCode: "\uf024"
                useSolidForAll: true
                autoToggle: false
                property var model: ModelInstances.getNotificationsModel()
                isChecked: model.unread
                attentionAnimation: isChecked
                checkedColor: "red"
                visible: isChecked

                onClicked: {
                    Common.openDialog("qrc:/qt/qml/NextAppUi/qml/NotificationsView.qml", appWindow, {});
                }
            }

            Text {
                id: cloudIcon
                text: "\uf0c2"
                font.family: ce.faSolidName
                font.styleName: ce.faSolidStyle
                color: NaComm.connected ? "green" : "red"
            }
        }
    }

    // Drawer Navigation
    Drawer {
        id: drawer
        width: 0.75 * parent.width
        height: parent.height

        DrawerContent {
            id: sidebar
            anchors.fill: parent

            Connections {
                target: sidebar
                function onSelectionChanged(left, right) {
                    console.log("Selection changed B ", left, " ", right)
                    dualView.setViews(left, right)
                }
            }
        }
    }

    DualView {
        id: dualView
        anchors.fill: parent
    }

    CommonElements {
        id: ce
    }

    function openWindow(name, args) {
        console.log("Creating QML window: " + name + " with args: " + JSON.stringify(args) )
        var component = Qt.createComponent("qrc:/qt/qml/NextAppUi/qml/" + name);
        if (component.status !== Component.Ready) {
            if(component.status === Component.Error )
                console.debug("Error:"+ component.errorString() );
            return;
        }
        var win = component.createObject(appWindow, args);
        win.show()
    }
}
