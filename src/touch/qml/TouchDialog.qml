import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    UiMetrics { id: metrics; pixelDensity: dialog.parent ? dialog.parent.Screen.pixelDensity : 0; pixelRatio: dialog.parent ? dialog.parent.Screen.devicePixelRatio : 1 }
    padding: metrics.inset
    background: Rectangle { color: "#121a1e"; radius: metrics.radius; border.color: "#334249" }
    header: RowLayout {
        implicitHeight: metrics.touchHeight + 2 * metrics.inset; spacing: metrics.gap
        Label {
            text: dialog.title; textFormat: Text.PlainText
            Layout.fillWidth: true; leftPadding: 20
            color: "#e6eef0"; font.pixelSize: 20; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        TouchButton {
            objectName: "dialogCloseButton"
            text: qsTr("close"); iconName: "close"; showLabel: true
            Layout.rightMargin: metrics.inset; Layout.topMargin: metrics.inset; Layout.bottomMargin: metrics.inset
            onClicked: dialog.reject()
        }
    }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
}
