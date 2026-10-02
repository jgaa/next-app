import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextAppUi
import Nextapp.Models
import "common.js" as Common

Dialog {
    id: root
    title: qsTr("Suggest next actions")
    parent: Overlay.overlay
    modal: false // Keep the calendar accessible for dragging suggestions.
    width: parent.width < 600 ? parent.width : Math.min(1000, parent.width * 0.85)
    height: parent.width < 600 ? parent.height : Math.min(850, parent.height * 0.9)
    x: (parent.width - width) / 2
    scale: calendarDragActive ? 0 : 1
    y: (parent.height - height) / 2
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Close
    onClosed: destroy()
    property var sourceIds: NaActionsModel.selectedIds.slice()
    property var categories: []
    property var selectedIds: []
    property var currentAction: null
    property bool requested: false
    property bool calendarDragActive: false
    readonly property bool filtersExpanded: filterToggle.checked
    header: Control {
        padding: 12
        bottomPadding: 6
        contentItem: RowLayout {
            Label {
                Layout.fillWidth: true
                text: root.title
                font.bold: true
                color: MaterialDesignStyling.onSurface
                elide: Text.ElideRight
            }
            StyledButton {
                id: filterToggle
                useWidth: 30
                text: ""
                checkable: true
                checked: true
                dim: checked
                Image {
                    source: "qrc:/qt/qml/NextAppUi/icons/filter.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(18, 18)
                    anchors.centerIn: parent
                }
                Accessible.name: checked ? qsTr("Hide filters") : qsTr("Show filters")
                ToolTip.visible: hovered || pressed
                ToolTip.delay: 500
                ToolTip.text: Accessible.name
                onToggled: if (!checked) results.forceActiveFocus()
            }
        }
    }
    palette.window: MaterialDesignStyling.surface
    palette.windowText: MaterialDesignStyling.onSurface
    palette.base: MaterialDesignStyling.surfaceContainer
    palette.text: MaterialDesignStyling.onSurface
    palette.button: MaterialDesignStyling.surfaceContainerHigh
    palette.buttonText: MaterialDesignStyling.onSurface
    palette.highlight: MaterialDesignStyling.primary
    palette.highlightedText: MaterialDesignStyling.onPrimary
    background: Rectangle {
        color: MaterialDesignStyling.surface
        border.color: MaterialDesignStyling.outlineVariant
        radius: root.width < 600 ? 0 : 8
    }

    function formatMinutes(minutes) {
        const hours = Math.floor(minutes / 60)
        const remainder = minutes % 60
        if (hours === 0) return qsTr("%1 min").arg(remainder)
        if (remainder === 0) return qsTr("%1 h").arg(hours)
        return qsTr("%1 h %2 min").arg(hours).arg(remainder)
    }

    function scheduleAction(action) {
        root.currentAction = action
        schedule.duration = action.duration
        schedule.startTime = NaCore.toDateAndTime(Math.ceil(Date.now() / 300000) * 300, 0)
        schedule.open()
    }
    readonly property var difficultyNames: [qsTr("Trivial"), qsTr("Easy"), qsTr("Normal"),
        qsTr("Hard"), qsTr("Very hard"), qsTr("Inspired")]

    function toggle(id) {
        let ids = selectedIds.slice()
        const index = ids.indexOf(id)
        if (index < 0) ids.push(id)
        else ids.splice(index, 1)
        selectedIds = ids
    }

    function edit(action) {
        Common.openDialog("EditActionDlg.qml", root.parent, {
            title: qsTr("Edit Action"), node: action.node,
            aprx: NaActionsModel.getAction(action.uuid)
        })
    }

    ActionSuggestionsModel { id: suggestions }
    CommonElements { id: ce }

    contentItem: ColumnLayout {
        spacing: 8
        ScrollView {
            id: filters
            property real revealHeight: root.filtersExpanded
                ? Math.min(form.implicitHeight, root.availableHeight * 0.45) : 0
            Layout.fillWidth: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: revealHeight
            Layout.maximumHeight: revealHeight
            visible: revealHeight > 0
            enabled: root.filtersExpanded
            clip: true
            Behavior on revealHeight {
                NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
            }
            GridLayout {
                id: form
                width: parent.width
                columns: root.width < 600 ? 1 : 2
                Label { text: qsTr("Current state") }
                ComboBox {
                    id: state
                    Layout.fillWidth: true
                    model: [qsTr("Exhausted"), qsTr("Normal"), qsTr("Good"), qsTr("Very good"), qsTr("Exceptional")]
                    currentIndex: 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Choose your energy level to match the difficulty of suggested actions.")
                }
                Label { text: qsTr("Available time") }
                RowLayout {
                    Layout.fillWidth: true
                    Slider {
                        id: available
                        Layout.fillWidth: true
                        from: 15
                        to: 480
                        stepSize: 15
                        snapMode: Slider.SnapAlways
                        value: 60
                        ToolTip.visible: hovered || pressed
                        ToolTip.delay: pressed ? 0 : 500
                        ToolTip.text: qsTr("Time you have to work: %1. From 15 minutes to 8 hours, in 15-minute steps.")
                            .arg(root.formatMinutes(Math.round(value)))
                    }
                    Label {
                        Layout.preferredWidth: 90
                        horizontalAlignment: Text.AlignRight
                        text: root.formatMinutes(Math.round(available.value))
                    }
                }
                Label { text: qsTr("Actions") }
                ComboBox {
                    id: scope
                    Layout.fillWidth: true
                    model: [qsTr("Active with a due date"), qsTr("Unset (no due date)"), qsTr("Both")]
                    currentIndex: 2
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Include actions with a due date, actions without a due date, or both.")
                }
                Label { text: qsTr("Source") }
                ComboBox {
                    id: source
                    Layout.fillWidth: true
                    model: [qsTr("All eligible actions"), qsTr("Selected actions (%1)").arg(root.sourceIds.length)]
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Search all eligible actions or only the rows selected in the actions list when this dialog opened.")
                }
                Label { text: qsTr("Lists") }
                ComboBox {
                    id: listScope
                    Layout.fillWidth: true
                    enabled: NaMainTreeModel.hasSelection
                    model: [qsTr("All"), qsTr("Selected list"), qsTr("Selected lists and sublists")]
                    onEnabledChanged: if (!enabled) currentIndex = 0
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Include all lists, the selected list only, or the selected list and its sublists.")
                }
                Label { text: qsTr("Categories (empty means all)") }
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        CategoryComboBox {
                            id: category
                            Layout.fillWidth: true
                            contentItem: Label {
                                text: category.displayText
                                color: MaterialDesignStyling.onSurface
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                leftPadding: 10
                                rightPadding: 24
                                Rectangle {
                                    anchors.right: parent.right
                                    width: 20
                                    height: parent.height
                                    visible: category.currentIndex >= 0
                                    color: category.currentIndex >= 0
                                        ? NaAcModel.get(category.currentIndex).color : "transparent"
                                }
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Choose one or more categories. Leave the filter empty to include all categories.")
                            onCategorySelected: (uuid) => {
                                if (!root.categories.includes(uuid)) root.categories = root.categories.concat([uuid])
                            }
                        }
                        Button {
                            text: qsTr("Clear")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Remove category filters and include all categories.")
                            onClicked: { root.categories = []; category.reset() }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.categories.map(id => NaAcModel.getName(id)).join(", ")
                    }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: qsTr("Find suggestions")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Rank matching actions by priority, due date, available time and difficulty.")
                enabled: !suggestions.busy
                onClicked: {
                    root.requested = true
                    root.selectedIds = []
                    root.currentAction = null
                    suggestions.suggest(state.currentIndex, Math.round(available.value), root.categories,
                        scope.currentIndex, source.currentIndex === 1, root.sourceIds,
                        listScope.enabled ? listScope.currentIndex : 0, NaMainTreeModel.selected)
                }
            }
            Button {
                text: qsTr("Modify selected (%1)").arg(root.selectedIds.length)
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Batch change the category, priority, difficulty or schedule of checked suggestions.")
                enabled: root.selectedIds.length > 0 && NaComm.connected
                onClicked: Common.openDialog("BatchActionsDlg.qml", root.parent, { selectedIds: root.selectedIds.slice() })
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: suggestions.busy ? qsTr("Finding suggestions…") : suggestions.error !== "" ? suggestions.error
                : root.requested && suggestions.suggestions.length === 0
                  ? qsTr("No matching actions. Try more time, another state, or broader filters.")
                  : qsTr("Ranked by priority, due date, time and difficulty. Drag a handle onto the calendar to schedule.")
        }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: suggestions.suggestions
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: suggestion
                required property var modelData
                required property int index
                width: results.width - (results.ScrollBar.vertical.visible ? results.ScrollBar.vertical.width : 0)
                padding: 4
                background: Rectangle {
                    radius: 8
                    color: root.currentAction?.uuid === suggestion.modelData.uuid
                        ? MaterialDesignStyling.surfaceContainerHighest
                        : suggestion.index % 2 ? MaterialDesignStyling.surface : MaterialDesignStyling.surfaceContainer
                }
                contentItem: RowLayout {
                    spacing: 4
                    CheckBoxWithFontIcon {
                        iconSize: 16
                        autoToggle: false
                        Layout.alignment: Qt.AlignVCenter
                        isChecked: root.selectedIds.includes(suggestion.modelData.uuid)
                        Accessible.name: qsTr("Select %1").arg(suggestion.modelData.name)
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Select this action for batch changes.")
                        onClicked: { root.toggle(suggestion.modelData.uuid); root.currentAction = suggestion.modelData }
                    }
                    ActionStateControl {
                        id: doneControl
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        Layout.rightMargin: 4
                        statusColor: suggestion.modelData.statusColor
                        scoreColor: suggestion.modelData.scoreColor
                        enabled: NaComm.connected
                        onClicked: NaActionsModel.markActionAsDone(suggestion.modelData.uuid, isChecked)
                    }
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: actionDetails.height - 4
                        color: suggestion.modelData.category
                            ? NaAcModel.getColorFromUuid(suggestion.modelData.category) : "transparent"
                        HoverHandler { id: categoryHover }
                        ToolTip.visible: categoryHover.hovered && suggestion.modelData.category !== ""
                        ToolTip.delay: 500
                        ToolTip.text: NaAcModel.getName(suggestion.modelData.category)
                    }
                    ColumnLayout {
                        id: actionDetails
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        Label {
                            id: actionTitle
                            Layout.fillWidth: true
                            color: doneControl.isChecked ? MaterialDesignStyling.onSurfaceVariant : MaterialDesignStyling.onSurface
                            text: suggestion.modelData.name
                            elide: Text.ElideRight
                            HoverHandler { id: titleHover }
                            ToolTip.visible: titleHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: suggestion.modelData.name + "\n"
                                + (suggestion.modelData.estimate > 0 ? root.formatMinutes(suggestion.modelData.estimate) : qsTr("No estimate"))
                                + " · " + root.difficultyNames[suggestion.modelData.difficulty]
                                + "\n" + qsTr("Double-click to edit this action.")
                            TapHandler {
                                onTapped: root.currentAction = suggestion.modelData
                                onDoubleTapped: root.edit(suggestion.modelData)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                visible: suggestion.modelData.due !== ""
                                text: suggestion.modelData.due
                                color: MaterialDesignStyling.onSurfaceVariant
                            }
                            Label {
                                font.family: ce.faSolidName
                                font.styleName: ce.faSolidStyle
                                text: "\uf802"
                                color: MaterialDesignStyling.onSurface
                            }
                            Label {
                                id: location
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                color: MaterialDesignStyling.outline
                                text: NaMainTreeModel.nodeNameFromUuid(suggestion.modelData.node, true)
                                elide: Text.ElideRight
                                HoverHandler { id: locationHover }
                                ToolTip.visible: locationHover.hovered
                                ToolTip.delay: 500
                                ToolTip.text: text
                            }
                            Label {
                                color: MaterialDesignStyling.onSurfaceVariant
                                visible: root.width >= 600
                                text: (suggestion.modelData.estimate > 0
                                    ? root.formatMinutes(suggestion.modelData.estimate) : qsTr("No estimate"))
                                    + " · " + root.difficultyNames[suggestion.modelData.difficulty]
                            }
                        }
                    }
                    ToolButton {
                        icon.source: "../icons/fontawsome/clock.svg"
                        icon.color: MaterialDesignStyling.onSurface
                        implicitWidth: 32
                        implicitHeight: 32
                        Accessible.name: qsTr("Start now")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Start working on this action now as the active work session.")
                        enabled: !doneControl.isChecked && NaComm.connected && NaCore.canAddLimitedResources
                        onClicked: {
                            NaWorkSessionsModel.startWorkSetActive(suggestion.modelData.uuid)
                            root.close()
                        }
                    }
                    ToolButton {
                        icon.source: "../icons/fontawsome/calendar-days.svg"
                        icon.color: MaterialDesignStyling.onSurface
                        implicitWidth: 32
                        implicitHeight: 32
                        Accessible.name: qsTr("Create time box")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Choose a date and time for a %1 time box, or drag the handle onto the calendar.")
                            .arg(root.formatMinutes(suggestion.modelData.duration))
                        enabled: !doneControl.isChecked && NaComm.connected && NaCore.canAddLimitedResources
                        onClicked: root.scheduleAction(suggestion.modelData)
                    }
                    ToolButton {
                        icon.source: "../icons/fontawsome/pen-to-square.svg"
                        icon.color: MaterialDesignStyling.onSurface
                        implicitWidth: 32
                        implicitHeight: 32
                        Accessible.name: qsTr("Edit action")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Open this action in the action editor.")
                        enabled: NaComm.connected
                        onClicked: root.edit(suggestion.modelData)
                    }
                    Label {
                        id: handle
                        text: "☷"
                        color: MaterialDesignStyling.onSurface
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        enabled: !doneControl.isChecked && NaComm.connected && NaCore.canAddLimitedResources
                        Accessible.name: qsTr("Drag action to calendar")
                        HoverHandler { id: handleHover; cursorShape: Qt.OpenHandCursor }
                        ToolTip.visible: handleHover.hovered && !drag.active
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Drag onto an existing time box to add this action, or onto empty calendar space to create a %1 time box. The dialog hides while dragging.")
                            .arg(root.formatMinutes(suggestion.modelData.duration))
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.MoveAction
                        Drag.mimeData: ({
                            "text/app.nextapp.action": suggestion.modelData.uuid,
                            "text/app.nextapp.curr.node": suggestion.modelData.node,
                            "text/app.nextapp.suggestion.duration": String(suggestion.modelData.duration),
                            "text/app.nextapp.suggestion.name": suggestion.modelData.name,
                            "text/app.nextapp.suggestion.category": suggestion.modelData.category
                        })
                        // Hide only after the native drag starts, keeping its source alive.
                        // Scaling avoids popup position clamping and close/destroy callbacks.
                        Drag.onDragStarted: root.calendarDragActive = true
                        Drag.onDragFinished: root.calendarDragActive = false
                        DragHandler {
                            id: drag
                            target: null
                            onActiveChanged: {
                                if (active) {
                                    root.currentAction = suggestion.modelData
                                    suggestion.grabToImage(function(result) {
                                        if (drag.active) { handle.Drag.imageSource = result.url; handle.Drag.active = true }
                                    })
                                } else handle.Drag.active = false
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: schedule
        title: qsTr("Create time box")
        modal: true
        parent: Overlay.overlay
        width: Math.min(parent.width, 450)
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        property alias startTime: start.text
        property alias duration: durationInput.value
        palette: root.palette
        background: Rectangle {
            color: MaterialDesignStyling.surface
            border.color: MaterialDesignStyling.outlineVariant
            radius: 8
        }
        property string error: ""
        onOpened: error = ""
        standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: qsTr("Start date and time") }
            TextField {
                id: start
                Layout.fillWidth: true
                placeholderText: qsTr("YYYY-MM-DD HH:MM")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Enter the date and local time when you want to start.")
            }
            Label { text: qsTr("Duration (minutes)") }
            SpinBox {
                id: durationInput
                from: 1
                to: 1440
                editable: true
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Duration in minutes, initially based on the estimate and your global time-box limits.")
            }
            Label { text: schedule.error; visible: text !== "" }
            Button {
                text: qsTr("Create")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Create the calendar time box with this action attached.")
                enabled: NaComm.connected && NaCore.canAddLimitedResources
                onClicked: {
                    const when = NaCore.parseDateOrTime(start.text, 0)
                    if (when > 0 && root.currentAction
                        && suggestions.createTimeBox(root.currentAction.uuid, when, durationInput.value)) {
                        schedule.close()
                    } else schedule.error = qsTr("Enter a valid start date and time.")
                }
            }
        }
    }
}
