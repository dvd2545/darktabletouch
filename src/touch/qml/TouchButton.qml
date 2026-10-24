import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitHeight: 64
    implicitWidth: Math.max(64, contentItem.implicitWidth + 32)
    focusPolicy: Qt.StrongFocus
    padding: 14
    opacity: enabled ? 1 : 0.35
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        color: control.checked ? "#101915" : "#e6e8eb"
        font.family: "Segoe UI"
        font.pixelSize: 15
        font.weight: control.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        radius: 12
        color: control.checked ? "#a9d5bc" : control.down ? "#3b4047" : control.hovered ? "#30343b" : "#24272d"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#a9d5bc" : control.checked ? "#a9d5bc" : "#353941"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
