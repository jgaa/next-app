import QtQuick
import QtQuick.Controls
import NextAppUi
import Nextapp.Models

// Shared state ring and priority/deadline checkmark for action rows.
CheckBoxWithFontIcon {
    id: root
    property int status: 0
    property color statusColor: "gray"
    property color scoreColor: "green"
    readonly property var statusIcons: ["\uf111", "\uf111", "\uf0c8"]
    checkedCode: "\uf111"
    uncheckedCode: statusIcons[Math.min(2, Math.max(0, status))]
    checkedColor: "green"
    uncheckedColor: statusColor
    iconSize: NaCore.isMobile ? 42 : 24
    Accessible.role: Accessible.CheckBox
    Accessible.name: isChecked ? qsTr("Mark action as active") : qsTr("Mark action as done")
    Accessible.checked: isChecked
    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: Accessible.name

    Text {
        anchors.fill: parent
        font.family: ce.faSolidName
        font.styleName: ce.faSolidStyle
        font.pixelSize: root.iconSize * 0.7
        text: "\uf00c"
        color: root.scoreColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    CommonElements { id: ce }
}
