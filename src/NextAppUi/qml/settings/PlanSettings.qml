import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NextAppUi
import Nextapp.Models 1.0

ScrollView {
    id: root
    anchors.fill: parent
    clip: true
    contentWidth: availableWidth

    property var planView: ({})
    readonly property bool hasSubscription: Boolean(planView.available)
    readonly property bool compactLayout: root.availableWidth < 420

    component ValueField: TextField {
        readOnly: true
        selectByMouse: true
        Layout.fillWidth: true
    }

    function refreshIfNeeded(forceRefresh) {
        if (!NaCore.plansEnabled) {
            return
        }
        NaCore.ensureCurrentPlanLoaded(forceRefresh)
    }

    function syncPlanView() {
        if (!visible) {
            return
        }
        planView = NaCore.currentPlanView
    }

    Component.onCompleted: syncPlanView()
    onVisibleChanged: {
        if (visible) {
            refreshIfNeeded(false)
            syncPlanView()
        }
    }

    Connections {
        target: NaCore
        function onPlansEnabledChanged() {
            if (NaCore.plansEnabled && root.visible) {
                root.refreshIfNeeded(false)
                root.syncPlanView()
            }
        }
        function onCurrentPlanChanged() {
            root.syncPlanView()
        }
    }

    ColumnLayout {
        width: Math.max(0, root.availableWidth - 16)
        spacing: 12

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Active Plan")
            font.bold: true
            font.pixelSize: 18
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: NaCore.currentPlanLoading
            visible: NaCore.currentPlanLoading
        }

        Frame {
            Layout.fillWidth: true
            visible: !NaCore.currentPlanLoading && root.hasSubscription

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                GridLayout {
                    columns: root.compactLayout ? 1 : 2
                    rowSpacing: 6
                    columnSpacing: 18
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Plan Name")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.planName || qsTr("Unknown")
                    }

                    Label {
                        text: qsTr("Plan Updated")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.planUpdated || qsTr("Not set")
                    }

                    Label {
                        text: qsTr("Plan Expires")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.planExpires || qsTr("No expiration")
                    }

                    Label {
                        text: qsTr("Grace Period Expires")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.gracePeriodExpires || qsTr("Not set")
                    }

                    Label {
                        text: qsTr("Account Expires")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.accountExpires || qsTr("No expiration")
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            visible: !NaCore.currentPlanLoading && root.hasSubscription

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("Effective Limits")
                    font.bold: true
                }

                GridLayout {
                    columns: root.compactLayout ? 1 : 2
                    rowSpacing: 6
                    columnSpacing: 18
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Users")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.usersLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Devices")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.devicesLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Nodes")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.nodesLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Actions")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.actionsLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Work Sessions")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.workSessionsLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Time Blocks")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.timeBlocksLimit || qsTr("Unlimited")
                    }

                    Label {
                        text: qsTr("Mobile Only")
                        font.bold: true
                        Layout.preferredWidth: root.compactLayout ? -1 : 170
                    }
                    ValueField {
                        text: root.planView.mobileOnly || qsTr("No")
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !NaCore.currentPlanLoading && !root.hasSubscription
            wrapMode: Text.WordWrap
            text: qsTr("Plan information is not available yet.")
        }

        Button {
            text: qsTr("Refresh")
            enabled: !NaCore.currentPlanLoading
            onClicked: root.refreshIfNeeded(true)
        }

        Button {
            text: qsTr("Payments")
            visible: !NaCore.isMobile // Not allowed on Google Play
            enabled: !NaCore.paymentsPageLoading
            onClicked: {
                paymentsProgress.open()
                NaCore.getPaymentsUrl()
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.preferredHeight: 8
        }
    }

    Dialog {
        id: paymentsProgress
        title: qsTr("Opening Payments")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.NoButton
        width: 320
        height: 180
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 12

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: NaCore.paymentsPageLoading
                visible: NaCore.paymentsPageLoading
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Contacting the payments service...")
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Cancel")
                onClicked: paymentsProgress.close()
            }
        }
    }

    Dialog {
        id: paymentsOpenedDialog
        title: qsTr("Payments Page Opened")
        modal: true
        standardButtons: Dialog.Ok
        width: 420
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The payments page has been opened in your default browser. You can copy the URL below if you want to use another browser.")
            }

            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: NaCore.lastPaymentsUrl
            }
        }
    }

    Connections {
        target: NaCore
        function onPaymentsPageLoadingChanged() {
            if (!NaCore.paymentsPageLoading && paymentsProgress.visible) {
                paymentsProgress.close()
            }
        }
        function onPaymentsPageOpened(url) {
            paymentsOpenedDialog.open()
        }
    }
}
