import QtQuick
import QtQuick.Controls

Popup {
    id: popup
    modal: true
    focus: true
    property var blockingWindow: null
    function releaseBlocking() {
        if(blockingWindow) {
            blockingWindow.blockingPopups = Math.max(0, blockingWindow.blockingPopups - 1)
            blockingWindow = null
        }
    }
    onVisibleChanged: {
        if(visible && modal) {
            const host = parent ? parent.Window.window : null
            if(host && host.blockingPopups !== undefined) {
                blockingWindow = host; host.blockingPopups++
            }
        } else releaseBlocking()
    }
    Component.onDestruction: releaseBlocking()
    UiMetrics { id: popupMetrics }
    padding: Math.round(14 * popupMetrics.paddingScale)
    background: Rectangle { color: "#131b1f"; radius: 8; border.color: "#334249" }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
}
