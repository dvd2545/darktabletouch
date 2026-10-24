import QtQuick
import QtQuick.Controls

Popup {
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
}
