import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property string iconName: ""
    property bool showLabel: true
    property bool stacked: false
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    Accessible.name: text
    ToolTip.visible: hovered && !!iconName
    ToolTip.text: text
    scale: down ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }
    implicitHeight: Math.max(metrics.touchHeight, contentItem.implicitHeight + topPadding + bottomPadding)
    implicitWidth: Math.max(metrics.touchHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    focusPolicy: Qt.StrongFocus
    padding: metrics.inset
    font.pixelSize: metrics.bodySize
    opacity: enabled ? 1 : 0.55
    contentItem: GridLayout {
        columns: control.stacked ? 1 : 2
        columnSpacing: 10; rowSpacing: 4
        Icon { Layout.alignment: Qt.AlignCenter; Layout.preferredWidth: 24; Layout.preferredHeight: 24; visible: !!control.iconName; name: control.iconName; color: control.checked ? "#07191b" : "#e6eef0" }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
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
        radius: metrics.radius
        color: control.checked ? "#00dbe5" : control.down ? "#304147" : control.hovered ? "#253439" : control.flat ? "transparent" : "#1c272c"
        border.width: control.activeFocus ? 2 : 0
        border.color: control.activeFocus || control.checked ? "#00dbe5" : "#334249"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
