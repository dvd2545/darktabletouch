import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: control
    implicitHeight: 64
    contentItem: Text {
        text: control.text; textFormat: Text.PlainText; font: control.font
        color: control.highlighted ? "#07191b" : "#e6eef0"
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.highlighted ? "#00dbe5" : control.hovered || control.down ? "#304147" : "#1c272c"
    }
}
