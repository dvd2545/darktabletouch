import QtQuick
import QtQuick.Controls

Control {
    id: control
    property bool checked: false
    property string text: ""
    signal clicked()
    signal pressAndHold()
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    implicitWidth: metrics.touchHeight; implicitHeight: metrics.touchHeight
    opacity: enabled ? 1 : 0.55
    focusPolicy: Qt.StrongFocus; hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.onPressAction: clicked()
    background: Rectangle {
        radius: metrics.radius
        color: tap.pressed ? "#304147" : control.checked ? "#153439" : control.hovered ? "#253439" : "#1c272c"
        border.width: control.activeFocus || control.checked ? 2 : 1
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
