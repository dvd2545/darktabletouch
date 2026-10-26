import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: control
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    implicitHeight: metrics.touchHeight
    leftPadding: metrics.inset; rightPadding: metrics.inset
    contentItem: Text {
        text: control.text; textFormat: Text.PlainText; font: control.font
        color: control.highlighted ? "#07191b" : "#e6eef0"
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.down ? (control.highlighted ? "#5de5eb" : "#345058") : control.highlighted ? "#00dbe5" : control.hovered ? "#304147" : "#1c272c"
    }
}
