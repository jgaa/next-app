import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models

Dialog {
    id: root
    title: qsTr("Modify selected actions")
    property var selectedIds: []
    modal: true
    parent: Overlay.overlay
    width: Math.min(parent.width, 600)
    height: Math.min(parent.height, 650)
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    standardButtons: Dialog.Cancel
    onClosed: destroy()

    // Each checked field is sent using the existing batch update APIs.
    contentItem: ScrollView {
        clip: true
        ColumnLayout {
            width: parent.width
            Label { text: qsTr("Modify %1 actions").arg(root.selectedIds.length) }
            CheckBox { id: changeDue; text: qsTr("Change when") }
            WhenSelector {
                id: when
                Layout.fillWidth: true
                enabled: changeDue.checked
                property bool chosen: false
                onDueWasSelected: chosen = true
            }
            CheckBox { id: changeCategory; text: qsTr("Set category") }
            CategoryComboBox {
                id: category
                Layout.fillWidth: true
                enabled: changeCategory.checked
            }
            CheckBox { id: clearCategory; text: qsTr("Clear category"); enabled: !changeCategory.checked }
            CheckBox { id: changePriority; text: qsTr("Set priority") }
            PrioritySelector {
                id: priority
                Layout.fillWidth: true
                enabled: changePriority.checked
            }
            CheckBox { id: changeDifficulty; text: qsTr("Set difficulty") }
            DifficultySelector {
                id: difficulty
                Layout.fillWidth: true
                enabled: changeDifficulty.checked
            }
            Button {
                text: qsTr("Apply changes")
                enabled: NaComm.connected && root.selectedIds.length > 0
                    && (changeDue.checked || changeCategory.checked || clearCategory.checked
                        || changePriority.checked || changeDifficulty.checked)
                    && (!changeDue.checked || when.chosen)
                    && (!changeCategory.checked || category.uuid !== "")
                    && (!changePriority.checked || priority.valid)
                    && (!changeDifficulty.checked || difficulty.currentIndex >= 0)
                onClicked: {
                    if (changeDue.checked) NaActionsModel.batchChangeDue(when.due, root.selectedIds)
                    if (changeCategory.checked || clearCategory.checked)
                        NaActionsModel.batchChangeCategory(changeCategory.checked ? category.uuid : "", root.selectedIds)
                    if (changePriority.checked) {
                        if (priority.mode === 0)
                            NaActionsModel.batchChangePriority(priority.priority, root.selectedIds)
                        else
                            NaActionsModel.batchChangeDynamicPriority(priority.urgency, priority.importance, root.selectedIds)
                    }
                    if (changeDifficulty.checked)
                        NaActionsModel.batchChangeDifficulty(difficulty.currentIndex, root.selectedIds)
                    root.accept()
                }
            }
        }
    }
}
