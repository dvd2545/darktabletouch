import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    padding: 16
    background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
    header: RowLayout {
        implicitHeight: 72; spacing: 12
        Label {
            text: dialog.title; textFormat: Text.PlainText
            Layout.fillWidth: true; leftPadding: 20
            color: "#e6eef0"; font.pixelSize: 20; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        TouchButton {
            objectName: "dialogCloseButton"
            text: qsTr("close"); iconName: "close"; showLabel: true
            Layout.rightMargin: 12
            onClicked: dialog.reject()
        }
    }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
}
