import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Button {
    id: control
    property string iconName: ""
    property bool showLabel: true
    property bool stacked: false
    property bool imageOverlay: false
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    Accessible.name: text
    ToolTip.visible: (hovered || activeFocus) && !!iconName
    ToolTip.text: text
    implicitHeight: Math.max(metrics.touchHeight, contentItem.implicitHeight + topPadding + bottomPadding)
    implicitWidth: Math.max(metrics.touchHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    focusPolicy: Qt.StrongFocus
    padding: metrics.inset
    font.pixelSize: metrics.bodySize
    opacity: enabled ? 1 : 0.55
    contentItem: GridLayout {
        layer.enabled: control.imageOverlay
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: "#000000"; shadowOpacity: 1; shadowBlur: 0.6; shadowVerticalOffset: 2; shadowHorizontalOffset: 0; autoPaddingEnabled: true }
        columns: control.stacked ? 1 : 2
        columnSpacing: 10; rowSpacing: 4
        Icon { Layout.alignment: Qt.AlignCenter; Layout.preferredWidth: 24; Layout.preferredHeight: 24; visible: !!control.iconName; name: control.iconName; shadowed: control.imageOverlay; color: control.imageOverlay ? (control.checked ? "#00dbe5" : "#ffffff") : control.checked ? "#07191b" : "#e6eef0" }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: !control.iconName || control.showLabel ? control.text : ""
            visible: !control.iconName || control.showLabel
            textFormat: Text.PlainText
            style: control.imageOverlay ? Text.Raised : Text.Normal; styleColor: "#a0000000"
            color: control.imageOverlay ? (control.checked ? "#00dbe5" : "#ffffff") : control.checked ? "#07191b" : "#e6eef0"
            font.family: control.font.family; font.pixelSize: control.font.pixelSize; font.weight: control.checked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
    }

    background: Rectangle {
        radius: metrics.radius
        color: control.imageOverlay ? "transparent" : control.down ? (control.checked ? "#5de5eb" : "#345058") : control.checked ? "#00bdc7" : control.hovered ? "#253439" : control.flat ? "transparent" : "#1c272c"
        border.width: (control.imageOverlay ? control.visualFocus : control.activeFocus) ? 2 : 0
        border.color: control.activeFocus || control.checked ? "#00dbe5" : "#334249"
    }
}
