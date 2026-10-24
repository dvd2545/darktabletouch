import QtQuick

Item {
    id: crop
    objectName: "cropOverlay"
    property real cropLeft: 0
    property real cropTop: 0
    property real cropRight: 1
    property real cropBottom: 1
    property real aspect: 0
    readonly property var bounds: [cropLeft, cropTop, cropRight, cropBottom]
    function setBounds(values) { cropLeft = values[0]; cropTop = values[1]; cropRight = values[2]; cropBottom = values[3] }
    function fitRatio() {
        if(aspect <= 0 || width <= 0 || height <= 0) return
        const ratio = aspect * height / width
        let w = cropRight - cropLeft, h = cropBottom - cropTop
        if(w / h > ratio) w = h * ratio; else h = w / ratio
        const x = (cropLeft + cropRight) / 2, y = (cropTop + cropBottom) / 2
        cropLeft = x - w / 2; cropRight = x + w / 2; cropTop = y - h / 2; cropBottom = y + h / 2
    }
    onAspectChanged: Qt.callLater(fitRatio)
    Rectangle { width: parent.width; height: crop.cropTop * parent.height; color: "#99000000" }
    Rectangle { y: crop.cropBottom * parent.height; width: parent.width; height: parent.height - y; color: "#99000000" }
    Rectangle { y: crop.cropTop * parent.height; width: crop.cropLeft * parent.width; height: (crop.cropBottom - crop.cropTop) * parent.height; color: "#99000000" }
    Rectangle { x: crop.cropRight * parent.width; y: crop.cropTop * parent.height; width: parent.width - x; height: (crop.cropBottom - crop.cropTop) * parent.height; color: "#99000000" }
    Rectangle {
        x: crop.cropLeft * crop.width; y: crop.cropTop * crop.height
        width: (crop.cropRight - crop.cropLeft) * crop.width; height: (crop.cropBottom - crop.cropTop) * crop.height
        color: "transparent"; border.color: "#00dbe5"; border.width: 2
        Repeater { model: 2; Rectangle { required property int index; x: parent.width * (index + 1) / 3; width: 1; height: parent.height; color: "#aaffffff" } }
        Repeater { model: 2; Rectangle { required property int index; y: parent.height * (index + 1) / 3; height: 1; width: parent.width; color: "#aaffffff" } }
        MouseArea {
            anchors.fill: parent
            property point start
            property var original
            onPressed: function(mouse) { start = mapToItem(crop, mouse.x, mouse.y); original = crop.bounds.slice() }
            onPositionChanged: function(mouse) {
                if(!pressed) return
                const point = mapToItem(crop, mouse.x, mouse.y)
                const dx = Math.max(-original[0], Math.min(1-original[2], (point.x-start.x)/crop.width))
                const dy = Math.max(-original[1], Math.min(1-original[3], (point.y-start.y)/crop.height))
                crop.setBounds([original[0]+dx, original[1]+dy, original[2]+dx, original[3]+dy])
            }
        }
    }
    Repeater {
        model: 4
        Item {
            required property int index
            readonly property bool west: index === 0 || index === 2
            readonly property bool north: index < 2
            x: (west ? crop.cropLeft : crop.cropRight) * crop.width - (west ? 0 : width)
            y: (north ? crop.cropTop : crop.cropBottom) * crop.height - (north ? 0 : height)
            width: 64; height: 64
            Rectangle { x: parent.west ? 0 : parent.width - width; y: parent.north ? 0 : parent.height - height; width: 22; height: 22; radius: 5; color: "#00dbe5"; border.color: "white" }
            MouseArea {
                anchors.fill: parent
                property point grabOffset
                property real oppositeX
                property real oppositeY
                onPressed: function(mouse) { oppositeX = parent.west ? crop.cropRight : crop.cropLeft; oppositeY = parent.north ? crop.cropBottom : crop.cropTop; const p = mapToItem(crop, mouse.x, mouse.y); grabOffset = Qt.point(p.x - (parent.west ? crop.cropLeft : crop.cropRight)*crop.width, p.y - (parent.north ? crop.cropTop : crop.cropBottom)*crop.height) }
                onPositionChanged: function(mouse) {
                    if(!pressed) return
                    const point = mapToItem(crop, mouse.x, mouse.y)
                    const p = Qt.point(point.x - grabOffset.x, point.y - grabOffset.y)
                    let w = Math.max(0.01, parent.west ? oppositeX - p.x/crop.width : p.x/crop.width - oppositeX)
                    let h = Math.max(0.01, parent.north ? oppositeY - p.y/crop.height : p.y/crop.height - oppositeY)
                    const maxW = parent.west ? oppositeX : 1-oppositeX, maxH = parent.north ? oppositeY : 1-oppositeY
                    if(crop.aspect > 0) {
                        const ratio = crop.aspect * crop.height/crop.width
                        w = Math.max(w, h*ratio); w = Math.min(w, maxW, maxH*ratio); h = w/ratio
                    } else { w = Math.min(w,maxW); h = Math.min(h,maxH) }
                    crop.cropLeft = parent.west ? oppositeX-w : oppositeX; crop.cropRight = parent.west ? oppositeX : oppositeX+w
                    crop.cropTop = parent.north ? oppositeY-h : oppositeY; crop.cropBottom = parent.north ? oppositeY : oppositeY+h
                }
            }
        }
    }
}
