import QtQuick
import QtQuick.Controls

Control {
    id: control
    property bool checked: false
    property string text: ""
    signal clicked()
    signal pressAndHold()
    implicitWidth: 64; implicitHeight: 64
    focusPolicy: Qt.StrongFocus; hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.onPressAction: clicked()
    background: Rectangle {
        radius: 12
        color: control.checked ? "#00dbe5" : tap.pressed ? "#304147" : control.hovered ? "#253439" : "#1c272c"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.checked ? "#00dbe5" : "#334249"
    }
    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        longPressThreshold: 0.6
        property bool held: false
        onPressedChanged: if(pressed) held = false
        onLongPressed: { held = true; control.forceActiveFocus(); control.pressAndHold() }
        onTapped: if(!held) { control.forceActiveFocus(); control.clicked() }
    }
    Keys.onSpacePressed: function(event) { clicked(); event.accepted = true }
    Keys.onReturnPressed: function(event) { clicked(); event.accepted = true }
}
