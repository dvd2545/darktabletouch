import QtQuick
import QtQuick.Controls

Control {
    id: control
    UiMetrics { id: metrics; pixelDensity: control.Screen.pixelDensity; pixelRatio: control.Screen.devicePixelRatio }
    property bool vertical: false
    required property real panelSize
    property real value: 0
    required property real minimumValue
    required property real maximumValue
    property real stepSize: 16
    signal adjusted(real delta)
    signal resetRequested()
    implicitWidth: vertical ? metrics.touchHeight : 112
    implicitHeight: vertical ? 112 : metrics.touchHeight
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Slider
    Accessible.description: qsTr("drag to resize; arrow keys adjust; double-tap or press confirm to reset")
    Accessible.onIncreaseAction: adjusted(stepSize)
    Accessible.onDecreaseAction: adjusted(-stepSize)
    ToolTip.visible: hover.hovered && !drag.active
    ToolTip.text: Accessible.description
    Binding { target: control; property: "value"; value: control.panelSize; restoreMode: Binding.RestoreNone }
    onValueChanged: if(value !== panelSize) adjusted(value - panelSize)

    Rectangle {
        anchors.centerIn: parent
        width: control.vertical ? 16 : 80; height: control.vertical ? 80 : 16
        radius: 8; color: control.activeFocus ? "#203c42" : "transparent"
        border.width: control.activeFocus ? 1 : 0; border.color: "#00dbe5"
    }
    Rectangle {
        anchors.centerIn: parent
        width: control.vertical ? 4 : 64; height: control.vertical ? 64 : 4
        radius: 2
        color: drag.active || control.activeFocus || hover.hovered ? "#00dbe5" : "#b5c8ce"
    }
    HoverHandler { id: hover; cursorShape: control.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor }
    DragHandler {
        id: drag
        target: null
        dragThreshold: 0
        property real startValue: 0
        function resizeFromPointer() {
            if(!active) return
            // the grip moves with the panel, so measure from the scene press
            const delta = control.vertical ? centroid.scenePosition.x - centroid.scenePressPosition.x
                : centroid.scenePosition.y - centroid.scenePressPosition.y
            control.adjusted(startValue - delta - control.value)
        }
        xAxis.enabled: control.vertical
        yAxis.enabled: !control.vertical
        cursorShape: control.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
        onActiveChanged: if(active) {
            startValue = control.value
            control.forceActiveFocus()
            resizeFromPointer()
        }
        onActiveTranslationChanged: resizeFromPointer()
    }
    TapHandler { onDoubleTapped: control.resetRequested() }
    Keys.onPressed: function(event) {
        if(event.key === Qt.Key_Right || event.key === Qt.Key_Up) { control.adjusted(stepSize); event.accepted = true }
        else if(event.key === Qt.Key_Left || event.key === Qt.Key_Down) { control.adjusted(-stepSize); event.accepted = true }
        else if([Qt.Key_Home, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].indexOf(event.key) >= 0) {
            control.resetRequested(); event.accepted = true
        }
    }
}
