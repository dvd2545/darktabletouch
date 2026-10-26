import QtQuick

QtObject {
    property real pixelDensity: 0
    property real pixelRatio: 1
    function targetSize(density, ratio) {
        if(!isFinite(density) || density < 2 || density > 20 || ratio <= 0) return 64
        return Math.max(56, Math.min(80, Math.round(9.5 * density / ratio)))
    }
    readonly property int touchHeight: targetSize(pixelDensity, pixelRatio)
    readonly property int gap: Math.round(touchHeight * 0.22)
    readonly property int inset: Math.round(touchHeight * 0.22)
    readonly property int bodySize: Math.round(15 * Math.max(1, touchHeight / 64))
    readonly property int captionSize: Math.round(bodySize * 0.87)
    readonly property int titleSize: Math.round(bodySize * 1.4)
    readonly property int panelWidth: Math.max(320, Math.round(touchHeight * 5.2))
    readonly property int compactFilmstrip: touchHeight + Math.ceil(touchHeight / 2) + 12
    readonly property int expandedFilmstrip: Math.round(touchHeight * 2.5)
    readonly property int radius: 8
    readonly property color canvas: "#090d0f"
    readonly property color panel: "#131b1f"
    readonly property color floating: "#263238"
    readonly property color text: "#e6eef0"
    readonly property color muted: "#a5b4bc"
    readonly property color accent: "#00dbe5"
}
