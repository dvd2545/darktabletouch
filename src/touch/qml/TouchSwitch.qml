import QtQuick
import QtQuick.Controls

Switch {
    id: control
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    implicitHeight: metrics.touchHeight
    implicitWidth: Math.max(metrics.touchHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    padding: Math.round(4 * metrics.paddingScale); spacing: metrics.gap
    font.pixelSize: metrics.bodySize
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.55
    indicator: Rectangle {
        implicitWidth: 48; implicitHeight: 28
        x: control.leftPadding; y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? (control.down ? "#5de5eb" : metrics.accent) : control.down ? "#52646b" : "#465157"
        Rectangle {
            width: 22; height: 22; radius: 11; y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? "#07191b" : metrics.text
        }
    }
    contentItem: Text {
        text: control.text; textFormat: Text.PlainText; font: control.font; color: metrics.text
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: metrics.radius; color: control.hovered ? "#253439" : "transparent"
        border.width: control.activeFocus ? 2 : 0; border.color: metrics.accent
    }
}
