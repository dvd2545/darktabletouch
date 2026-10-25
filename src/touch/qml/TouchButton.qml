import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property string iconName: ""
    property bool showLabel: false
    Accessible.name: text
    ToolTip.visible: hovered && !!iconName
    ToolTip.text: text
    scale: down ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }
    implicitHeight: 64
    implicitWidth: Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding)
    focusPolicy: Qt.StrongFocus
    padding: 14
    font.pixelSize: 15
    opacity: enabled ? 1 : 0.35
    contentItem: RowLayout {
        spacing: 12
        Icon { Layout.alignment: Qt.AlignVCenter; width: 24; height: 24; visible: !!control.iconName; name: control.iconName; color: control.checked ? "#07191b" : "#e6eef0" }
        Text {
            Layout.fillWidth: true
            text: !control.iconName || control.showLabel ? control.text : ""
            visible: !control.iconName || control.showLabel
            textFormat: Text.PlainText
            color: control.checked ? "#07191b" : "#e6eef0"
            font.family: control.font.family; font.pixelSize: control.font.pixelSize; font.weight: control.checked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
    }

    background: Rectangle {
        radius: 12
        color: control.checked ? "#00dbe5" : control.down ? "#304147" : control.hovered ? "#253439" : "#1c272c"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.checked ? "#00dbe5" : "#334249"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
